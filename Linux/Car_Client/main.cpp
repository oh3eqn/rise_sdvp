/*
    Copyright 2016 Benjamin Vedder	benjamin@vedder.se

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

#include <QCoreApplication>
#include "carclient.h"
#include <QDebug>
#include <signal.h>

void showHelp()
{
    qDebug() << "Arguments";
    qDebug() << "-h, --help : Show help text";
    qDebug() << "-p, --ttyport : Serial port, e.g. /dev/ttyUSB0";
    qDebug() << "-b, --baudrate : Serial baud rate, e.g. 9600";
    qDebug() << "-l, --log : Log to file, e.g. /tmp/logfile.bin (TODO!)";
    qDebug() << "--tcprtcmport : TCP server port for RTCM data";
    qDebug() << "--tcpnmeasrv : NMEA server address";
    qDebug() << "--tcpnmeaport : NMEA server port";
}

static void m_cleanup(int sig)
{
    (void)sig;
    qApp->quit();
    qDebug() << "Bye :)";
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    QStringList args = QCoreApplication::arguments();
    QString ttyPort = "/dev/ttyACM0";
    QString logFile = "";
    int baudrate = 115200;
    int tcpRtcmPort = 8200;
    QString tcpNmeaServer = "127.0.0.1";
    int tcpNmeaPort = 2948;

    signal(SIGINT, m_cleanup);
    signal(SIGTERM, m_cleanup);

    for (int i = 0;i < args.size();i++) {
        // Skip the program argument
        if (i == 0) {
            continue;
        }

        QString str = args.at(i).toLower();

        // Skip path argument
        if (i >= args.size() && args.size() >= 3) {
            break;
        }

        bool dash = str.startsWith("-") && !str.startsWith("--");
        bool found = false;

        if ((dash && str.contains('h')) || str == "--help") {
            showHelp();
            found = true;
            return 0;
        }

        if ((dash && str.contains('p')) || str == "--ttyport") {
            if ((i - 1) < args.size()) {
                i++;
                ttyPort = args.at(i);
                found = true;
            }
        }

        if ((dash && str.contains('b')) || str == "--baudrate") {
            if ((i - 1) < args.size()) {
                i++;
                bool ok;
                baudrate = args.at(i).toInt(&ok);
                found = ok;
            }
        }

        if ((dash && str.contains('l')) || str == "--log") {
            if ((i - 1) < args.size()) {
                i++;
                logFile = args.at(i);
                found = true;
            }
        }

        if (str == "--tcprtcmport") {
            if ((i - 1) < args.size()) {
                i++;
                bool ok;
                tcpRtcmPort = args.at(i).toInt(&ok);
                found = ok;
            }
        }

        if (str == "--tcpnmeasrv") {
            if ((i - 1) < args.size()) {
                i++;
                tcpNmeaServer = args.at(i);
                found = true;
            }
        }

        if (str == "--tcpnmeaport") {
            if ((i - 1) < args.size()) {
                i++;
                bool ok;
                tcpNmeaPort = args.at(i).toInt(&ok);
                found = ok;
            }
        }

        if (!found) {
            if (dash) {
                qCritical() << "At least one of the flags is invalid:" << str;
            } else {
                qCritical() << "Invalid option:" << str;
            }

            showHelp();
            return 1;
        }
    }

    CarClient car;
    car.connectSerial(ttyPort, baudrate);
    car.connectNmea(tcpNmeaServer, tcpNmeaPort);
    car.startRtcmServer(tcpRtcmPort);

    return a.exec();
}