///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Marcel, VE3UKW <marcel@ve3ukw.ca>                          //
// Some code by AI                                                               //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
// (at your option) any later version.                                           //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include <QDebug>
#include <QHostAddress>
#include <QHostInfo>
#include <QTimer>
#include <QUdpSocket>

#include "udpptt.h"

// Lives on UDPPTT's thread, so the socket and timer are only used from there
class UDPPTTWorker : public QObject
{
    Q_OBJECT
public:
    UDPPTTWorker() :
        m_socket(nullptr),
        m_heartbeat(nullptr),
        m_release(nullptr),
        m_port(0),
        m_tailMs(0),
        m_ptt(false),
        m_sentPtt(false)
    {}

    void start()
    {
        m_socket = new QUdpSocket(this);
        m_socket->bind(QHostAddress::AnyIPv4, 0);
        connect(m_socket, &QUdpSocket::readyRead, this, &UDPPTTWorker::readReplies);
        m_heartbeat = new QTimer(this);
        m_heartbeat->setInterval(UDPPTT::m_heartbeatMs);
        connect(m_heartbeat, &QTimer::timeout, this, [this]() { send("PTT 1"); });
        m_release = new QTimer(this);
        m_release->setSingleShot(true);
        connect(m_release, &QTimer::timeout, this, &UDPPTTWorker::release);
    }

    void stop()
    {
        if (m_sentPtt) {
            release();
        }

        delete m_release;
        m_release = nullptr;
        delete m_heartbeat;
        m_heartbeat = nullptr;
        delete m_socket;
        m_socket = nullptr;
    }

    void setDestination(const QString& host, quint16 port)
    {
        m_port = port;
        m_address = QHostAddress(host);

        if (m_address.isNull())
        {
            QHostInfo info = QHostInfo::fromName(host);

            if (!info.addresses().isEmpty()) {
                m_address = info.addresses().first();
            } else {
                emit error(QString("Cannot resolve PTT host %1").arg(host));
            }
        }
    }

    void setPtt(bool on)
    {
        if (on == m_ptt) {
            return;
        }

        m_ptt = on;

        if (on)
        {
            m_release->stop();

            if (!m_sentPtt)
            {
                m_sentPtt = true;
                send("PTT 1");
                m_heartbeat->start();
            }
        }
        else if (m_tailMs > 0)
        {
            m_release->start(m_tailMs); // heartbeat keeps going until then
        }
        else
        {
            release();
        }
    }

    void setTailMs(int tailMs) {
        m_tailMs = tailMs;
    }

    void requestStatus() {
        send("STATUS");
    }

signals:
    void replyReceived(const QString& reply);
    void error(const QString& message);

private:
    QUdpSocket *m_socket;
    QTimer *m_heartbeat;
    QTimer *m_release;
    QHostAddress m_address;
    quint16 m_port;
    int m_tailMs;
    bool m_ptt;     //!< Requested state
    bool m_sentPtt; //!< State sent to the sequencer, which lags m_ptt by the tail

    void release()
    {
        m_heartbeat->stop();
        m_sentPtt = false;
        send("PTT 0");
    }

    void send(const char *command)
    {
        if (!m_socket || m_address.isNull() || (m_port == 0)) {
            return;
        }

        QByteArray datagram(command);

        if (m_socket->writeDatagram(datagram, m_address, m_port) != datagram.size()) {
            emit error(QString("PTT send failed: %1").arg(m_socket->errorString()));
        }
    }

    void readReplies()
    {
        while (m_socket->hasPendingDatagrams())
        {
            QByteArray datagram(m_socket->pendingDatagramSize(), 0);
            m_socket->readDatagram(datagram.data(), datagram.size());
            emit replyReceived(QString::fromUtf8(datagram).trimmed());
        }
    }
};

UDPPTT::UDPPTT(QObject *parent) :
    QObject(parent),
    m_worker(new UDPPTTWorker()),
    m_ptt(false)
{
    m_thread.setObjectName("UDPPTT");
    m_worker->moveToThread(&m_thread);
    connect(m_worker, &UDPPTTWorker::replyReceived, this, &UDPPTT::replyReceived);
    connect(m_worker, &UDPPTTWorker::error, this, &UDPPTT::error);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, [this]() { m_worker->start(); }, Qt::QueuedConnection);
}

UDPPTT::~UDPPTT()
{
    // Release PTT before the thread goes, rather than relying on the sequencer's heartbeat timeout
    QMetaObject::invokeMethod(m_worker, [this]() { m_worker->stop(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void UDPPTT::setDestination(const QString& host, quint16 port)
{
    QMetaObject::invokeMethod(m_worker, [this, host, port]() { m_worker->setDestination(host, port); }, Qt::QueuedConnection);
}

void UDPPTT::setPtt(bool on)
{
    if (m_ptt.exchange(on) == on) {
        return;
    }

    QMetaObject::invokeMethod(m_worker, [this, on]() { m_worker->setPtt(on); }, Qt::QueuedConnection);
}

void UDPPTT::setTailMs(int tailMs)
{
    QMetaObject::invokeMethod(m_worker, [this, tailMs]() { m_worker->setTailMs(tailMs); }, Qt::QueuedConnection);
}

void UDPPTT::requestStatus()
{
    QMetaObject::invokeMethod(m_worker, [this]() { m_worker->requestStatus(); }, Qt::QueuedConnection);
}

#include "udpptt.moc"
