/*
 * Copyright (C) 2024
 *
 * This file is part of LumosLiDARViewer.
 *
 * LumosLiDARViewer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * LumosLiDARViewer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with LumosLiDARViewer.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CMAINWIN_H
#define CMAINWIN_H

#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>

#include "CLumoMap.h"
#include "CCloudPoints.h"
#include "CComm.h"
#include <QtCore/QObject>

class CMainWin : public QMainWindow {
    Q_OBJECT

public:
    CMainWin(QWidget *parent = nullptr) : QMainWindow(parent), cloudPoints(new CCloudPoints(this)), lumoMap(new CLumoMap(this)) {
        setCentralWidget(lumoMap);
        setUI();
        // 데이터 갱신 타이머
        QObject::connect(&coolTimer, &QTimer::timeout, this, &CMainWin::updatePoints);
    }
    ~CMainWin() {
        if (comm)
            comm->close();
    }

public slots:
    void updatePoints() {
        if (comm->isIdle() && comm->inbox()) {
            comm->recv(buff, msgWaitFor, true);
        }
        else // Drawing Data Test
        {
            cloudPoints->generateVirtualData();
            lumoMap->lumos(cloudPoints->getPoints());
        }
    }

    void onStatus(Comm *sender, Comm::eStatus status) {
        if (sender && sender->isOnError()) {
            commAlert->setStyleSheet("color: white; background-color: red; padding: 2px;");
        }
        else {
            commAlert->setStyleSheet("");
            commAlert->clear();
        }

        switch(status) {
        case Comm::eStatus::connected:
            connStatus->setText("Connected");
            if (!btnConnect->isChecked())
                btnConnect->setChecked(true);
            chkCommType->setEnabled(false);
        case Comm::eStatus::ready:
            connStatus->setStyleSheet("color: white; background-color: Green; padding: 2px;");
            break;
        case Comm::eStatus::sending:
        case Comm::eStatus::recving:
            connStatus->setStyleSheet("color: black; background-color: lightGreen; padding: 2px;");
            break;
        case Comm::eStatus::sent:
            buff.clear();
            break;
        case Comm::eStatus::recved:
            afterRecved();
            QtConcurrent::run([&]() {
                lumoMap->lumos(cloudPoints->getPoints());
            });
            break;
        case Comm::eStatus::connFailed:
        case Comm::eStatus::connLost:
            onAlert(nullptr, 0, "Connection Error");
        case Comm::eStatus::closed:
            connStatus->setStyleSheet("color: white; background-color: darkRed; padding: 2px;");
            connStatus->setText("Disconnected");
            if (btnConnect->isChecked())
                btnConnect->setChecked(false);
            chkCommType->setEnabled(true);
            break;
        case Comm::eStatus::connecting:
            onAlert(nullptr, 0, "Connecting...");
            break;
        case Comm::eStatus::disconnFailed:
            onAlert(nullptr, 0, "Disconnting Failed.");
            break;
        case Comm::eStatus::sendFailed:
            onAlert(nullptr, 0, "Sending Failed.");
            break;
        case Comm::eStatus::recvFailed:
            onAlert(nullptr, 0, "Receiving Failed.");
            break;
        }
    }
    void onProgress(Comm *sender, Comm::eProgress progress, quint32 bytes) {
        if (comm->isBusy()) {
            QString progMsg((progress == Comm::eProgress::sending? "Sending: ": "Receiving: ") + QString::number(bytes));
            commAlert->setText(progMsg);
        }
    }
    void onAlert(Comm *sender, int alertCode, const QString msg) {
        msgTimer.stop();
        commAlert->clear();
        if (sender) {
            QString alertMsg("Error: " + msg);
            commAlert->setText(alertMsg);
        }
        else {
            commAlert->setText(msg);
        }
        msgTimer.start(msgWaitFor);
    }

private:
    QByteArray buff;

    CCloudPoints *cloudPoints;
    CLumoMap *lumoMap;
    QLabel *statusIndicator;
    QTimer coolTimer, msgTimer;

    QLabel *connStatus, *commAlert;
    QString ipAddress;
    int port;
    enum class eCommType { None, TCP, UDP, COM };
    eCommType m_commType = eCommType::TCP;
    QLineEdit *connString;
    QLineEdit *connNum;
    QPushButton *btnConnect;
    QAction *chkTCP;
    QAction *chkUDP;
    QAction *chkSerial;
    QActionGroup *chkCommType;
    Comm *comm = nullptr;
    const quint32 commWaitFor = 1000;
    const quint32 msgWaitFor = 5000;

    void afterRecved() {
        float lidarFactor[2] = { 0 };
        int cnt = comm->bytesRecv();
        QDataStream in(&buff, QIODevice::ReadOnly);
        //in.setByteOrder(QDataStream::LittleEndian);
        in.setFloatingPointPrecision(QDataStream::SinglePrecision);
        while(!in.atEnd())
        {
            in >> lidarFactor[0] >> lidarFactor[1];
            cloudPoints->setPoint(lidarFactor[0], lidarFactor[1]);
        }
        buff.clear();
    }

    bool setCommType() {
        if (comm) {
            return false;

            while (!comm->close(commWaitFor));
            delete comm;
            comm = nullptr;
        }

        if (chkTCP->isChecked()) {
            m_commType = eCommType::TCP;
            comm = new TCPComm(this);
        }
        else if (chkUDP->isChecked()) {
            m_commType = eCommType::UDP;
            comm = new UDPComm(this);
        }
        else if (chkSerial->isChecked()) {
            m_commType = eCommType::COM;
            //comm = new SerialComm(this);
        }
        QObject::connect(comm, &Comm::onStatus, this, &CMainWin::onStatus);
        QObject::connect(comm, &Comm::onAlert, this, &CMainWin::onAlert);
        QObject::connect(comm, &Comm::onProgress, this, &CMainWin::onProgress);
        return true;
    }

    void toggleConn() {
       if (btnConnect->isChecked()) {
            if (!comm)
                setCommType();
            if (!comm->checkConn()) {
                comm->setConnInfo(connString->text(), connNum->text().toInt());
                if (comm->connect(commWaitFor))
                    coolTimer.start(100); // 100ms마다 화면 갱신
            }
        }
        else {
            if (comm) {
                coolTimer.stop();
                if (!comm->isOnError()) {
                    comm->close(commWaitFor);
                }
                comm->deleteLater();
                comm = nullptr;
            }
        }
    }

    void setUI() {
        this->resize(1280, 720);
        // 메뉴바 구성
        // QMenu *fileMenu = menuBar()->addMenu("File");
        // QAction *exitAction = fileMenu->addAction("Exit");
        // QObject::connect(exitAction, &QAction::triggered, this, &CMainWin::close);


        // ipAddress와 port 멤버 변수 초기화
        QString ipAddress = "127.0.0.1"; // 기본값 설정
        quint32 port = 45454; // 기본값 설정

        // 툴바 설정
        QToolBar *toolBar = addToolBar("Control");

        // 툴바: 통신 타입 선택 라디오 버튼 추가
        chkCommType = new QActionGroup(this);
        chkTCP = new QAction("TCP", chkCommType);
        chkTCP->setCheckable(true);
        chkTCP->setChecked(true);
        chkUDP = new QAction("UDP", chkCommType);
        chkUDP->setCheckable(true);
        chkSerial = new QAction("COM", chkCommType);
        chkSerial->setCheckable(true);
        toolBar->addActions(chkCommType->actions());
        // QObject::connect(chkTCP, &QAction::triggered, this, &CMainWin::setCommType);
        // QObject::connect(chkUDP, &QAction::triggered, this, &CMainWin::setCommType);
        // QObject::connect(chkSerial, &QAction::triggered, this, &CMainWin::setCommType);

        // 툴바: IP 주소 및 포트 번호 입력란 추가
        connString = new QLineEdit("127.0.0.1", this);
        connString->setFixedWidth(100);
        connString->setAlignment(Qt::AlignCenter);
        connString->setPlaceholderText("Enter IP Address");
        toolBar->addWidget(connString);

        connNum = new QLineEdit("45454", this);
        connNum->setFixedWidth(100);
        connNum->setAlignment(Qt::AlignCenter);
        connNum->setPlaceholderText("Enter Port Number");
        connNum->setValidator(new QIntValidator(0, 65535, this));
        toolBar->addWidget(connNum);

        // 툴바: 통신 연결/종료 토글 버튼 추가
        btnConnect = new QPushButton("Connect", this);
        btnConnect->setCheckable(true);
        btnConnect->setChecked(false);
        toolBar->addWidget(btnConnect);
        QObject::connect(btnConnect, &QPushButton::toggled, this, &CMainWin::toggleConn);

        // 상태표시줄-통신 설정
        QStatusBar *statusBar = new QStatusBar(this);
        setStatusBar(statusBar);
        // 상태표시줄: connectionStatus를 고정된 크기로 표시하는 QLabel 위젯 추가
        connStatus = new QLabel("Disconnected", this);
        connStatus->setFixedWidth(100);
        connStatus->setAlignment(Qt::AlignCenter);
        statusBar->addPermanentWidget(connStatus);

        // 상태표시줄: alert을 확장하여 표시하는 QLabel 위젯 추가
        commAlert = new QLabel(this);
        statusBar->addWidget(commAlert);

        this->onStatus(comm, Comm::eStatus::closed);

        //일정 시간 후 상태알림바 메시지 삭제
        msgTimer.setSingleShot(true);
        QObject::connect(&msgTimer, &QTimer::timeout, [&]() {
            commAlert->clear();
            commAlert->setStyleSheet("");
        });
    }
};

#endif // CMAINWIN_H
