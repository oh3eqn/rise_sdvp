#include "networklogger.h"
#include "ui_networklogger.h"
#include <QMessageBox>
#include <QDebug>
#include "nmeaserver.h"
#include "utility.h"
#include <cmath>
#include <QFileDialog>
#include <QDateTime>

NetworkLogger::NetworkLogger(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::NetworkLogger)
{
    ui->setupUi(this);

    mTcpSocket = new QTcpSocket(this);
    mTcpConnected = false;
    mFixNowStr = "Solution...";
    mSatNowStr = "Sats...";
    memset(&mGpsState, 0, sizeof(mGpsState));
    mPing = new Ping(this);

    connect(mTcpSocket, SIGNAL(readyRead()), this, SLOT(tcpInputDataAvailable()));
    connect(mTcpSocket, SIGNAL(connected()), this, SLOT(tcpInputConnected()));
    connect(mTcpSocket, SIGNAL(disconnected()),
            this, SLOT(tcpInputDisconnected()));
    connect(mTcpSocket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(tcpInputError(QAbstractSocket::SocketError)));
    connect(mPing, SIGNAL(pingRx(int,QString)), this, SLOT(pingRx(int,QString)));
    connect(mPing, SIGNAL(pingError(QString,QString)), this, SLOT(pingError(QString,QString)));
}

NetworkLogger::~NetworkLogger()
{
    if (mLog.isOpen()) {
        qDebug() << "Closing log:" << mLog.fileName();
        mLog.close();
    }

    delete ui;
}

void NetworkLogger::tcpInputConnected()
{
    ui->nmeaServerConnectButton->setEnabled(true);
    ui->nmeaServerConnectButton->setText("Disconnect");
    mTcpConnected = true;
}

void NetworkLogger::tcpInputDisconnected()
{
    ui->nmeaServerConnectButton->setEnabled(true);
    ui->nmeaServerConnectButton->setText("Connect");
    mTcpConnected = false;
}

void NetworkLogger::tcpInputDataAvailable()
{
    QByteArray nmea_msg =  mTcpSocket->readAll();

    NmeaServer::nmea_gga_info_t gga;
    QTextStream msgs(nmea_msg);
    bool gga_set = false;

    while(!msgs.atEnd()) {
        QString str = msgs.readLine();
        QByteArray data = str.toLocal8Bit();

        // Hack
        if (str == "$GPGSA,A,1,,,,,,,,,,,,,,,*1E") {
            mFixNowStr = "Solution: Invalid";
            mSatNowStr = "Satellites: 0";
        }

        if (NmeaServer::decodeNmeaGGA(data, gga) >= 0) {
            mSatNowStr.sprintf("Satellites: %d", gga.n_sat);

            //qDebug() << data;
            //qDebug() << QString().sprintf("%.9f", gga.lat);

            switch (gga.fix_type) {
            case 0: mFixNowStr = "Solution: Invalid"; break;
            case 1: mFixNowStr = "Solution: SPP"; break;
            case 2: mFixNowStr = "Solution: DGPS"; break;
            case 3: mFixNowStr = "Solution: PPS"; break;
            case 4: mFixNowStr = "Solution: RTK Fix"; break;
            case 5: mFixNowStr = "Solution: RTK Float"; break;
            default: mFixNowStr = "Solution: Unknown"; break;
            }

            ui->nmeaFixTypeLabel->setText(mFixNowStr);
            ui->nmeaSatsLabel->setText(mSatNowStr);

            gga_set = true;
        }
    }

    if (gga_set && (gga.fix_type == 1 || gga.fix_type == 2 || gga.fix_type == 4 || gga.fix_type == 5)) {
        if (mPing->isRunning()) {
            // Return if previous ping is not finished yet
            return;
        }

        double x, y, z;

        utility::llhToXyz(gga.lat, gga.lon, gga.height, &x, &y, &z);

        mGpsState.lat = gga.lat;
        mGpsState.lon = gga.lon;
        mGpsState.height = gga.height;
        mGpsState.fix_type = gga.fix_type;
        mGpsState.sats = gga.n_sat;
        mGpsState.ms = gga.t_tow;
        mGpsState.x = x;
        mGpsState.y = y;
        mGpsState.z = z;

        QString logStr;
        logStr.sprintf("%s    %.8f    %.8f    %.3f    %d    %d",
                       QDateTime::currentDateTime().toString(Qt::ISODate).toLocal8Bit().data(),
                       gga.lat, gga.lon, gga.height, gga.fix_type, ui->pingLenBox->value());
        bool pingOk = mPing->pingHost(ui->pingHostEdit->text(), ui->pingLenBox->value(), logStr);

        // Convert to local ENU frame if initialized
        if (mGpsState.local_init_done) {
            float dx = (float)(mGpsState.x - mGpsState.ix);
            float dy = (float)(mGpsState.y - mGpsState.iy);
            float dz = (float)(mGpsState.z - mGpsState.iz);

            mGpsState.lx = mGpsState.r1c1 * dx + mGpsState.r1c2 * dy + mGpsState.r1c3 * dz;
            mGpsState.ly = mGpsState.r2c1 * dx + mGpsState.r2c2 * dy + mGpsState.r2c3 * dz;
            mGpsState.lz = mGpsState.r3c1 * dx + mGpsState.r3c2 * dy + mGpsState.r3c3 * dz;

            // Apply offsets and rotation for local position
            const float s_rot = sinf(mGpsState.orot);
            const float c_rot = cosf(mGpsState.orot);
            float px = mGpsState.lx * c_rot + mGpsState.ly * s_rot + mGpsState.ox;
            float py = mGpsState.lx * s_rot + mGpsState.ly * c_rot + mGpsState.oy;

            // Plot local position on map
            if (pingOk) {
                mLastPoint.setXY(px, py);

                QString info;
                info.sprintf("Lat  : %.8f\n"
                             "Lon  : %.8f\n"
                             "H    : %.3f\n"
                             "Fix  : %d\n"
                             "Pktzs: %d\n",
                             gga.lat, gga.lon, gga.height, gga.fix_type, ui->pingLenBox->value());

                mLastPoint.setInfo(info);
            }
        } else {
            initGpsLocal(&mGpsState);
        }
    }
}

void NetworkLogger::tcpInputError(QAbstractSocket::SocketError socketError)
{
    (void)socketError;

    QString errorStr = mTcpSocket->errorString();
    qWarning() << "TcpError:" << errorStr;
    QMessageBox::warning(0, "BaseStation TCP Error", errorStr);

    mTcpSocket->close();

    ui->nmeaServerConnectButton->setEnabled(true);
    ui->nmeaServerConnectButton->setText("Connect");
    mTcpConnected = false;
}

void NetworkLogger::pingRx(int time, QString msg)
{
    QString msStr;
    msStr.sprintf("%.3f ms", (double)time / 1000.0);
    ui->pingMsLabel->setText(msStr);

    if (!msg.isEmpty()) {
        QString msStr;
        msStr.sprintf("    %.3f", (double)time / 1000.0);

        QString logLine;
        logLine += msg + msStr;
        if (ui->logPrintBox->isChecked()) {
            ui->logBrowser->append(logLine);
        }

        if (mLog.isOpen()) {
            mLog.write(QString(logLine + "\r\n").toLocal8Bit());
        }

        if (ui->plotMapBox->isChecked()) {
            QString info = mLastPoint.getInfo();
            msStr.sprintf("Time : %.3f ms", (double)time / 1000.0);
            info.append(msStr);
            mLastPoint.setInfo(info);
            ui->mapWidget->addInfoPoint(mLastPoint);
        }
    }
}

void NetworkLogger::pingError(QString msg, QString error)
{
    ui->pingMsLabel->setText("error");

    if (msg.isEmpty()) {
        QMessageBox::warning(this, "Ping error", error);
    } else {
        QString logLine;
        logLine += msg + "    error";

        if (ui->logPrintBox->isChecked()) {
            ui->logBrowser->append(logLine);
        }

        if (mLog.isOpen()) {
            mLog.write(QString(logLine + "\r\n").toLocal8Bit());
        }

        if (ui->plotMapBox->isChecked()) {
            QString info = mLastPoint.getInfo();
            info.append("Time : error");
            mLastPoint.setInfo(info);
            ui->mapWidget->addInfoPoint(mLastPoint);
        }
    }
}

void NetworkLogger::on_nmeaServerConnectButton_clicked()
{
    if (mTcpConnected) {
        mTcpSocket->abort();

        ui->nmeaServerConnectButton->setEnabled(true);
        ui->nmeaServerConnectButton->setText("Connect");
        mTcpConnected = false;
    } else {
        mTcpSocket->abort();
        mTcpSocket->connectToHost(ui->nmeaServerEdit->text(), ui->nmeaServerPortBox->value());

        ui->nmeaServerConnectButton->setEnabled(false);
        ui->nmeaServerConnectButton->setText("Connecting...");
    }
}

void NetworkLogger::initGpsLocal(GPS_STATE *gps)
{
    gps->ix = gps->x;
    gps->iy = gps->y;
    gps->iz = gps->z;

    float so = sinf((float)gps->lon * M_PI / 180.0);
    float co = cosf((float)gps->lon * M_PI / 180.0);
    float sa = sinf((float)gps->lat * M_PI / 180.0);
    float ca = cosf((float)gps->lat * M_PI / 180.0);

    // ENU
    gps->r1c1 = -so;
    gps->r1c2 = co;
    gps->r1c3 = 0.0;

    gps->r2c1 = -sa * co;
    gps->r2c2 = -sa * so;
    gps->r2c3 = ca;

    gps->r3c1 = ca * -co;
    gps->r3c2 = ca * -so;
    gps->r3c3 = sa;

    gps->lx = 0.0;
    gps->ly = 0.0;
    gps->lz = 0.0;

    // Set offset to 0 for now
    gps->ox = 0.0;
    gps->oy = 0.0;

    gps->local_init_done = true;
}

void NetworkLogger::on_pingTestButton_clicked()
{
    mPing->pingHost(ui->pingHostEdit->text(), ui->pingLenBox->value());
}

void NetworkLogger::on_logClearButton_clicked()
{
    ui->logBrowser->clear();
}

void NetworkLogger::on_logFileChooseButton_clicked()
{
    QString path;
    path = QFileDialog::getSaveFileName(this, tr("Choose where to save the network log"));
    if (path.isNull()) {
        return;
    }

    ui->logFileEdit->setText(path);
}

void NetworkLogger::on_logFileActiveBox_toggled(bool checked)
{
    if (checked) {
        if (mLog.isOpen()) {
            mLog.close();
        }

        mLog.setFileName(ui->logFileEdit->text());
        bool ok = mLog.open(QIODevice::ReadWrite | QIODevice::Append);

        if (!ok) {
            QMessageBox::warning(this, "Log",
                                 "Could not open log file.");
            ui->logFileActiveBox->setChecked(false);
        }
    } else {
        mLog.close();
    }
}

void NetworkLogger::on_mapClearButton_clicked()
{
    ui->mapWidget->clearInfoTrace();
}