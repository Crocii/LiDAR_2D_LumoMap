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

#ifndef COMM_H
#define COMM_H

#include <QtCore/QFuture>
#include <QFutureWatcher>
#include <QtCore/QTimer>
#include <QtCore/QObject>

#include <QtCore/QMutex>
#include <QtConcurrent>


#define THREAD_BEGIN    QtConcurrent::run([&]() {
#define THREAD_END      });

#ifndef _WINBASE_
#define IGNORE              0       // Ignore signal
#define INFINITE		0xFFFFFFFF  // Infinite timeout
#endif

#define comm_noErr (\
m_status < eStatus::onError)
#define comm_onErr (\
m_status >= eStatus::onError)
#define comm_home (\
m_status == eStatus::closed || \
m_status == eStatus::closing)
#define comm_idle (\
m_status == eStatus::ready)
#define comm_busy (\
m_status == eStatus::sending || \
m_status == eStatus::recving)
#define comm_done (\
m_status == eStatus::sent || \
m_status == eStatus::recved)


//*===============================================================*//
//*                        Astract Classe                         *//
//*===============================================================*//

class Comm : public QObject {
    Q_OBJECT

public:
    Comm(QObject *parent = nullptr, int commID = 0)
        : QObject(parent), connWatchdog(this), progTimeout(this)
    {
        m_commID = commID;
        m_status = eStatus::closed;
        //m_statErr = eStatus::noErrStat;

        QObject::connect(&connWatchdog, &QTimer::timeout, this, [&]() {
            if (!this->m_isClosed) {
                checkConn(false);
            }
        });

        progTimeout.setSingleShot(true);
        QObject::connect(&progTimeout, &QTimer::timeout, this, [&]() {
            if (worker.isRunning()) {
                worker.cancel();
                if (m_status == eStatus::sending) {
                    setStatus(eStatus::sendFailed);
                }
                else if(m_status == eStatus::recving) {
                    setStatus(eStatus::recvFailed);
                }
                else if(m_status == eStatus::connecting) {
                    setStatus(eStatus::connFailed);
                }
                else if (m_status == eStatus::ready) {
                    emit onProgress(this, eProgress::inbox, m_bytesInbox);
                    QCoreApplication::processEvents();
                }
            }
        });

        futureTimer.setSingleShot(true);
        QObject::connect(&futureTimer, &QTimer::timeout, this, [&]() {
            future.cancel();
        });
    }
    virtual ~Comm() override {
        m_isClosed = true;
        m_status = eStatus::closed;
        connWatchdog.stop();
        progTimeout.stop();
        futureMtx.~QMutex();
    }
private:
    QFuture<bool> future;
    QTimer futureTimer;
    QMutex futureMtx;

    bool runINTime(bool enableTimeout, quint32 timeout, std::function<bool()> func) {
        QMutexLocker locker(&futureMtx);

        bool ret = false;
        if (enableTimeout && timeout && timeout < INFINITE) {
            future.cancel();
            future = QtConcurrent::run(func);
            futureTimer.start(timeout);
            future.waitForFinished();
            futureTimer.stop();
            ret = (!future.isCanceled() && future.result());
        }
        else
            ret = func();

        return ret;
    }

public:
    // Events
    enum class eStatus : unsigned int
    {
        closed = 0,			//정상 종료 상태.
        connecting,			//연결 시도 이벤트 As Master, Listening As Device
        connected,			//연결 완료 이벤트, 상태 점검 후 자동으로 ready로 넘어감.
        ready,				//idle 상태, 이전 단계 정상 완료, 다음 단계 진행 할 수 있음 확인.
        sending,			//Tx 시작/진행 이벤트.
        sent,				//Tx 완료 이벤트.
        recving,			//Rx 시작/진행 이벤트.
        recved,				//Rx 완료 이벤트.
        closing,
        onError = 1000,
        connLost,
        connFailed,
        disconnFailed,
        sendFailed,
        recvFailed,
    };

    enum class eProgress : unsigned int
    {
        sending = eStatus::sending,
        inbox = eStatus::recved,
        recving = eStatus::recving,
    };

    bool setConnInfo(QString connString, int connNum = 0, void* connInfo = nullptr) {
        m_connInfo = connInfo;
        m_connString = connString;
        m_connNum = connNum;
        m_connAvailable = setConnInfoProc(m_connString, m_connNum, m_connInfo);
        return m_connAvailable;
    }

public:
    bool connect(quint32 timeout = INFINITE) {
        if (!m_connAvailable)
            return false;

        m_isClosed = false;

        setStatus(eStatus::connecting);
        m_isConnected = this->runINTime(m_enableConnTimeout, timeout, [&]()->bool {
            return connectProc(timeout);
        });
        if (m_isConnected)
            setStatus(eStatus::connected);
        else
            setStatus(eStatus::connFailed);
        return m_isConnected;
    }

    bool close(quint32 timeout = INFINITE) {
        m_isClosed = true;
        if (!checkConnProc())
            return true;

        setStatus(eStatus::closing);
        m_isConnected = !closeProc(timeout);

        if (!m_isConnected)
            setStatus(eStatus::closed);
        else
            setStatus(eStatus::disconnFailed);
        return m_isConnected;
    }

private:
    Q_INVOKABLE bool doSendProc(QByteArray &data, quint32 timeout) {
        QMutexLocker locker(&commMtx);

        bool ret = false;
        m_bytesSent = 0;

        setStatus(eStatus::sending);
        if (m_enableSendTimeout && timeout && timeout < INFINITE) {
            progTimeout.start(timeout);
            ret = sendProc(data, timeout);
            progTimeout.stop();
        }
        else
            ret = sendProc(data, timeout);

        if (ret)
            setStatus(eStatus::sent);
        else
            setStatus(eStatus::sendFailed);

        return ret;
    }

public:
    bool send(QByteArray &data, quint32 timeout = INFINITE, bool async = false) {
        if (!comm_idle)
            return false;

        if (async) {
            worker = QtConcurrent::run([this, &data, timeout]() {
                QMetaObject::invokeMethod(this, "doSendProc",
                                          Qt::BlockingQueuedConnection,
                                          Q_ARG(QByteArray&, data),
                                          Q_ARG(quint32, timeout));
            });
        }
        else {
            return doSendProc(data, timeout);
        }
    }

private:
    Q_INVOKABLE bool doInboxProc(quint32 timeout) {
        QMutexLocker locker(&commMtx);

        bool ret = false;
        m_bytesInbox = 0;

        if (m_enableRecvTimeout && timeout && timeout < INFINITE) {
            progTimeout.start(timeout);
            ret = inboxProc(timeout);
            progTimeout.stop();
        }
        else
            ret = inboxProc(timeout);
        if (ret) {
            emit onProgress(this, eProgress::inbox, m_bytesInbox);
            QCoreApplication::processEvents();
        }
        return ret;
    }


public:
    bool inbox(quint32 timeout = IGNORE, bool async = false) {
        if (!comm_idle)
            return false;

        if (async) {
            worker = QtConcurrent::run([this, timeout]() {
                QMetaObject::invokeMethod(this, "doInboxProc",
                                          Qt::BlockingQueuedConnection,
                                          Q_ARG(quint32, timeout));
            });
            return true;
        }
        else {
            return doInboxProc(timeout);
        }
    }

private:
    Q_INVOKABLE bool doRecvProc(QByteArray &data, quint32 timeout) {

        QMutexLocker locker(&commMtx);

        bool ret = false;
        m_bytesRecv = 0;

        setStatus(eStatus::recving);
        if (m_enableRecvTimeout && timeout && timeout < INFINITE) {
            progTimeout.start(timeout);
            ret = recvProc(data, timeout);
            progTimeout.stop();
        }
        else
            ret = recvProc(data, timeout);

        if (ret)
            setStatus(eStatus::recved);
        else
            setStatus(eStatus::recvFailed);

        return ret;
    }

public:
    bool recv(QByteArray &buffer, quint32 timeout = INFINITE, bool async = false) {
        if (!comm_idle)
            return false;

        if (async) {
            worker = QtConcurrent::run([this, &buffer, timeout]() {
                QMetaObject::invokeMethod(this, "doRecvProc",
                                          Qt::BlockingQueuedConnection,
                                          Q_ARG(QByteArray&, buffer),
                                          Q_ARG(quint32, timeout));
            });
            return true;
        }
        else {
            return doRecvProc(buffer, timeout);
        }
    }

    int bytesRecv() const {
        return m_bytesRecv;
    }

    int bytesSent() const {
        return m_bytesSent;
    }

    eStatus getStatus() const {
        return m_status;
    }

    bool checkConn(bool emergency = false) {
        if (m_isClosed) {
            m_isConnected = false;
            return false;
        }
        if (!emergency)
            QMutexLocker locker(&commMtx);
        if (this->checkConnProc()) {
            if (!m_isConnected) {
                m_isConnected = true;
                setStatus(eStatus::connected);
            }
        }
        else {
            if (m_isConnected) {
                m_isConnected = false;
                setStatus(eStatus::connLost);
            }
        }
        return m_isConnected;
    }

    bool reconnect() {
        if (checkConnProc())
            closeProc();
        connectProc();
        //commMtx.unlock();
        return checkConn();
    }

    bool isConnected() const {
        return m_isConnected;
    }

    bool isOnError() {
        return comm_onErr;
    }

    bool isIdle() {
        return comm_idle;
    }

    bool isAtHome() {
        return comm_home;
    }

    bool isBusy() {
        return comm_busy;
    }

    bool isDone() {
        return comm_done;
    }

public:
    // Connection monitoring
    void setTimeout(bool checkConnAlive, int interval,
                     bool connTimeout, bool sendTimeout, bool recvTimeout) {
        if (checkConnAlive) {
            if (connWatchdog.isActive())
                connWatchdog.stop();
            connWatchdog.start(interval);
        }
        else {
            connWatchdog.stop();
        }
        m_enableConnTimeout = connTimeout;
        m_enableSendTimeout = sendTimeout;
        m_enableRecvTimeout = recvTimeout;
    }

protected:
    //Must be implemented.
    virtual bool setConnInfoProc(QString connString, int connNum = 0, void* connInfo = nullptr) = 0;
    virtual bool connectProc(quint32 timeout = INFINITE) const = 0;
    virtual bool closeProc(quint32 timeout = INFINITE) const = 0;
    virtual bool sendProc(QByteArray &data, quint32 timeout = INFINITE) = 0;
    virtual bool inboxProc(quint32 timeout = IGNORE) = 0;
    virtual bool recvProc(QByteArray &buffer, quint32 timeout = INFINITE) = 0;
    virtual bool checkConnProc(bool emergency = false) const = 0;

private:
    void setStatus(eStatus status) {
        if (m_status == status) return;

        m_status = status;
        emit onStatus(this, status);
        QCoreApplication::processEvents();

        switch (m_status) {
        case eStatus::connected:
        case eStatus::sent:
        case eStatus::recved:
            QtConcurrent::run([&, this]() {
                m_status = eStatus::ready;
                QMetaObject::invokeMethod(this, "onStatus",
                                          Qt::BlockingQueuedConnection,
                                          Q_ARG(Comm*, this),
                                          Q_ARG(eStatus, eStatus::ready));
            });
            break;
        default:
            break;
        }
    }

protected:
    void setProgress(eProgress progress, quint32 bytesTotal) {
        progTimeout.stop();
        emit onProgress(this, progress, bytesTotal);
        QCoreApplication::processEvents();
        progTimeout.start(m_timeout);
    }

    void raiseAlert(int alertCode, const QString msg) {
        emit onAlert(this, alertCode, msg);
        QCoreApplication::processEvents();
    }

    bool isClosed() const {
        return m_isClosed;
    }

signals:
    void onStatus(Comm *sender, eStatus status);
    void onProgress(Comm *sender, eProgress progress, quint32 bytes);
    void onAlert(Comm *sender, int alertCode, const QString msg);

protected:
    int m_commID = 0;
    void *m_connInfo = nullptr;
    QString m_connString;
    int m_connNum = 0;
    bool m_connAvailable = false;

    eStatus m_status = eStatus::closed;
    //eStatErr m_statErr = eStatus::noErrStat;
    bool m_isClosed = true;
    bool m_isConnected = false;
    int m_bytesSent = 0;
    int m_bytesRecv = 0;
    int m_bytesInbox = 0;

    QTimer connWatchdog;
    QTimer progTimeout;
    QFuture<void> worker;
    QMutex commMtx;

private:
    quint32 m_inbox = 0;
    quint32 m_timeout = INFINITE;
    bool m_enableConnTimeout = false, m_enableSendTimeout = false, m_enableRecvTimeout = false;
};

#include <QtCore/QByteArray>
#include <QtNetwork/QHostAddress>

//*===============================================================*//
//*                        Dirived Classes                        *//
//*===============================================================*//

// TCPComm class
#include <QtNetwork/QTcpSocket>
class TCPComm : public Comm {
    Q_OBJECT

public:
    TCPComm(QObject *parent = nullptr, int commID = 0)
        : Comm(parent, commID), socket(new QTcpSocket(this)) {
        QObject::connect(socket, &QTcpSocket::errorOccurred, this, &TCPComm::handleError);
        QObject::connect(socket, &QTcpSocket::disconnected, this, &TCPComm::handleLostConn);
        QObject::connect(socket, &QTcpSocket::stateChanged, this, &TCPComm::handleStateChanged);
    }
    ~TCPComm() { socket->close(); }

protected:
    bool setConnInfoProc(QString connString, int connNum, void* connInfo) override {
        bool isOK = (connString.count('.') >= 3) &&
                    (connNum > 0 && connNum <= 65535);
        if (!isOK)
            return false;
        hostAddr.setAddress(connString);
        m_port = (quint16)connNum;
        return true;
    }

    bool connectProc(quint32 timeout = INFINITE) const override {
        if (!m_connAvailable)
            return false;
        if (checkConnProc())
            return true;
        socket->connectToHost(hostAddr, m_port);
        if (timeout)
            socket->waitForConnected(timeout);
        return this->checkConnProc();
    }

    bool closeProc(quint32 timeout = INFINITE) const override {
        if (socket->state() == QAbstractSocket::UnconnectedState)
            return true;
        socket->disconnectFromHost();
        if (timeout)
            socket->waitForDisconnected(timeout);
        return socket->state() == QAbstractSocket::UnconnectedState;
    }

    bool sendProc(QByteArray &data, quint32 timeout = INFINITE) override {
        m_bytesSent = socket->write(data);
        if (timeout)
            socket->waitForBytesWritten(timeout);
        return m_bytesSent == data.size();
    }

    bool inboxProc(quint32 timeout = IGNORE) override {
        if (timeout)
            socket->waitForReadyRead(timeout);
        m_bytesInbox = socket->bytesAvailable();
        return m_bytesInbox > 0;
    }

    bool recvProc(QByteArray &buffer, quint32 timeout = INFINITE) override {
        int recvBytes = socket->bytesAvailable();
        if (timeout && !recvBytes) {
            socket->waitForReadyRead(timeout);
            recvBytes = socket->bytesAvailable();
        }
        buffer = socket->readAll();
        m_bytesRecv = buffer.size();
        return (bool)m_bytesRecv;
    }

    bool checkConnProc(bool emergency = false) const override {
        return socket->state() == QAbstractSocket::ConnectedState;
    }

private:
    QTcpSocket *socket;
    quint16 m_port;
    QHostAddress hostAddr;

    void handleError(QAbstractSocket::SocketError err) {
        Q_UNUSED(err);
        this->raiseAlert((int)err, socket->errorString());
    }

    void handleLostConn() {
        if (!this->isClosed())
            checkConn(true);
    }

    void handleStateChanged(QAbstractSocket::SocketState stat) {

    }
};

// UDPComm class
#include <QtNetwork/QUdpSocket>
class UDPComm : public Comm {
    Q_OBJECT

public:

    UDPComm(QObject *parent = nullptr, int commID = 0)
        : Comm(parent, commID), socket(new QUdpSocket(this)) {
        QObject::connect(socket, &QUdpSocket::errorOccurred, this, &UDPComm::handleError);
        QObject::connect(socket, &QUdpSocket::disconnected, this, &UDPComm::handleLostConn);
    }
    ~UDPComm() { socket->close(); }

protected:
    bool setConnInfoProc(QString connString, int connNum, void* connInfo) override {
        bool isOK = (connNum > 0 && connNum <= 65535);
        if (!isOK)
            return false;
        if (connString.count('.') != 3)
            hostAddr = QHostAddress::AnyIPv4;
        else
            hostAddr.setAddress(connString);
        m_port = (quint16)connNum;
        return true;
    }

    bool connectProc(quint32 timeout = INFINITE) const override {
        if (!m_connAvailable)
            return false;
        if (checkConnProc())
            return true;

        bool isOK = socket->bind(hostAddr, m_port);
        if (isOK) {
            socket->connectToHost(hostAddr, m_port);
            if (timeout)
                socket->waitForConnected(timeout);
            return this->checkConnProc();
        }
    }

    bool closeProc(quint32 timeout = INFINITE) const override {
        if (socket->state() == QAbstractSocket::UnconnectedState)
            return true;
        socket->disconnectFromHost();
        if (timeout)
            socket->waitForDisconnected(timeout);
        return socket->state() == QAbstractSocket::UnconnectedState;
    }

    bool sendProc(QByteArray &data, quint32 timeout = INFINITE) override {
        m_bytesSent = socket->writeDatagram(data, hostAddr, m_port);
        if (timeout)
            socket->waitForBytesWritten(timeout);
        return m_bytesSent == data.size();
    }

    bool inboxProc(quint32 timeout = IGNORE) override {
        if (timeout)
            socket->waitForReadyRead(timeout);
        m_bytesInbox = socket->bytesAvailable();
        return m_bytesInbox > 0;
    }

    bool recvProc(QByteArray &buffer, quint32 timeout = INFINITE) override {
        int recvBytes = socket->bytesAvailable();
        if (timeout && !recvBytes) {
            socket->waitForReadyRead(timeout);
            recvBytes = socket->bytesAvailable();
        }
        buffer = socket->readAll();
        m_bytesRecv = buffer.size();
        return (bool)m_bytesRecv;
    }

    bool checkConnProc(bool emergency = false) const override {
        QAbstractSocket::SocketState curStat = socket->state();
        return curStat == QAbstractSocket::ConnectedState;
    }

private:
    QUdpSocket *socket;
    quint16 m_port;
    QHostAddress hostAddr;

    void handleError(QAbstractSocket::SocketError err) {
        Q_UNUSED(err);
        raiseAlert((int)err, socket->errorString());
    }

    void handleLostConn() {
        if (!this->isClosed())
            checkConn(true);
    }
};

#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QList>
class SerialComm : public Comm {
    Q_OBJECT

public:
    SerialComm(QObject *parent = nullptr, int commID = 0)
        : Comm(parent, commID), serial(new QSerialPort(this)) {
        QObject::connect(serial, &QSerialPort::errorOccurred, this, &SerialComm::handleError);
        //QObject::connect(serial, &QSerialPort::readyRead, this, &SerialComm::handleReadyRead);
    }
    ~SerialComm() { serial->close(); }

protected:
    bool setConnInfoProc(QString connString, int connNum, void* connInfo) override {
        Q_UNUSED(connNum)
        Q_UNUSED(connInfo)
        m_connAvailable = false;

        serial->setPortName(connString);
        serial->setBaudRate(connNum);

        m_connAvailable = true;
        return m_connAvailable;
    }

    bool connectProc(quint32 timeout = INFINITE) const override {

        if (!m_connAvailable) return false;

        if (serial->isOpen())
            return true;
        return serial->open(QIODevice::ReadWrite);
    }

    bool closeProc(quint32 timeout = INFINITE) const override {
        Q_UNUSED(timeout)

        if (serial->isOpen()) {
            serial->close();
        }

        return !serial->isOpen();
    }

    bool sendProc(QByteArray &data, quint32 timeout = INFINITE) override {
        m_bytesSent = serial->write(data);
        if (timeout)
            serial->waitForBytesWritten(timeout);
        return m_bytesSent == data.size();
    }

    bool inboxProc(quint32 timeout = IGNORE) override {
        if (timeout)
            serial->waitForReadyRead(timeout);
        m_bytesInbox = serial->bytesAvailable();
        return m_bytesInbox > 0;
    }

    bool recvProc(QByteArray &buffer, quint32 timeout = INFINITE) override {
        int recvBytes = serial->bytesAvailable();
        if (timeout && !recvBytes) {
            serial->waitForReadyRead(timeout);
            recvBytes = serial->bytesAvailable();
        }
        buffer = serial->readAll();
        m_bytesRecv = buffer.size();
        return (bool)m_bytesRecv;
    }

    bool checkConnProc(bool emergency = false) const override {
        Q_UNUSED(emergency)
        return serial->isOpen();
    }

    // ... Implement other methods from Comm ...
    // QList<QSerialPortInfo>* getComports() {
    //     comports.clear();
    //     comports = QSerialPortInfo::availablePorts();
    //     return &comports;
    // }

    class Config {
    public:
        QList<QSerialPortInfo>* comports;
        QSerialPortInfo currentPort;
        QSerialPort::BaudRate baudRate;
    };

    // Config* getConfig() {
    //     if (!m_connInfo)
    //         m_connInfo = &hConfig;
    //     return m_connInfo;
    // }

    // void setConfig(Config* config) {
    //     m_connInfo = config;
    // }

private:
    Config hConfig = { 0 };
    Config *m_connInfo = nullptr;
    QSerialPort *serial = nullptr;
    bool m_connAvailable = false;
    QList<QSerialPortInfo> comports;

    // void handleError(QSerialPort::SerialPortError err) {
    //     if (err == QSerialPort::ResourceError)
    //         checkConn(true);
    //     raiseAlert((int)err, serial->errorString());
    // }

    void handleError(QSerialPort::SerialPortError err) {
        if (err != QSerialPort::NoError) {
            this->raiseAlert((int)err, serial->errorString());
        }
    }

// private:
//     QSerialPort *serial;

//     void handleError(QSerialPort::SerialPortError err) {
//         if (err != QSerialPort::NoError) {
//             this->raiseAlert((int)err, serial->errorString());
//         }
//     }

//     void handleReadyRead() {
//         if (serial->bytesAvailable()) {
//             emit onProgress(this, eProgress::inbox, serial->bytesAvailable());
//             QCoreApplication::processEvents();
//         }
//     }

};

#endif // COMM_H
