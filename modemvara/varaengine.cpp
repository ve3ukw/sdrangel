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

#include <cmath>
#include <mutex>

#include <QDebug>

#include <xara.h>

#include "varaengine.h"
#include "varatxlink.h"

static_assert(VARAEngine::m_sampleRate == XARA_SAMPLE_RATE, "xara sample rate");
static_assert(VARAEngine::m_blockSize == XARA_BLOCK, "xara block size");

namespace {

// All engines, for xara's log lines, which come through one callback for the whole process
QMutex engineListMutex;
QList<VARAEngine*> engineList;
// The engine calling xara on this thread, whose modem the log lines are from. On xara's own threads there is none.
thread_local VARAEngine *currentEngine = nullptr;

struct CurrentEngine
{
    VARAEngine *m_previous;
    CurrentEngine(VARAEngine *engine) : m_previous(currentEngine) { currentEngine = engine; }
    ~CurrentEngine() { currentEngine = m_previous; }
};

void logLine(void *user, int level, const char *line)
{
    (void) user;
    VARAEngine::log(level, line);
}

}

bool VARAEngine::Config::operator==(const Config& other) const
{
    return (m_mode == other.m_mode)
        && (m_call == other.m_call)
        && (m_address == other.m_address)
        && (m_cmdPort == other.m_cmdPort)
        && (m_kissPort == other.m_kissPort)
        && (m_driveDb == other.m_driveDb)
        && (m_registered == other.m_registered);
}

VARAEngine::VARAEngine() :
    m_modem(nullptr),
    m_in(m_blockSize, 0.0f),
    m_out(m_blockSize, 0.0f),
    m_inFill(0),
    m_rxLag(0),
    m_txLatency(0),
    m_txLinked(false),
    m_flags(0)
{
    static std::once_flag logOnce;
    std::call_once(logOnce, []() { xara_set_log(logLine, nullptr); });
    QMutexLocker listLock(&engineListMutex);
    engineList.append(this);
}

VARAEngine::~VARAEngine()
{
    {
        QMutexLocker listLock(&engineListMutex); // after this the log callback is never called
        engineList.removeOne(this);
    }

    QMutexLocker lock(&m_mutex);
    destroy();
}

void VARAEngine::setLogCallback(const LogCallback& callback)
{
    QMutexLocker listLock(&engineListMutex);
    m_logCallback = callback;
}

void VARAEngine::log(int level, const char *line)
{
    qDebug("xara: %s", line); // the info lines are in the VARA Demod's log, so only debug here

    if (level != XARA_LOG_INFO) {
        return;
    }

    QDateTime dateTime = QDateTime::currentDateTime();
    QString text = QString::fromUtf8(line);
    QMutexLocker listLock(&engineListMutex);

    for (VARAEngine *engine : engineList)
    {
        if (engine->m_logCallback && (!currentEngine || (currentEngine == engine))) {
            engine->m_logCallback(dateTime, text);
        }
    }
}

void VARAEngine::setConfig(const Config& config)
{
    QMutexLocker lock(&m_mutex);
    CurrentEngine current(this);

    if (m_modem && (config == m_config)) {
        return;
    }

    destroy();
    m_config = config;
    create();
}

void VARAEngine::setRxLag(int samples)
{
    QMutexLocker lock(&m_mutex);
    m_rxLag = std::max(0, samples);

    if (m_modem) {
        xara_set_tx_latency(m_modem, m_txLatency, m_rxLag);
    }
}

void VARAEngine::create()
{
    m_error.clear();

    if (m_config.m_call.isEmpty())
    {
        m_error = "Set a call sign";
        return;
    }

    QByteArray call = m_config.m_call.toUtf8();
    QByteArray address = m_config.m_address.toUtf8();
    xara_config cfg;
    xara_config_default(&cfg);
    cfg.mode = m_config.m_mode;
    cfg.call = call.constData();
    cfg.addr = address.isEmpty() ? nullptr : address.constData();
    cfg.cmd_port = m_config.m_cmdPort;
    cfg.kiss_port = m_config.m_kissPort;
    cfg.drive_db = m_config.m_driveDb;
    cfg.registered = m_config.m_registered;
    cfg.tx_latency = m_txLatency;
    cfg.rx_lag = m_rxLag;
    char err[256];
    m_modem = xara_create(&cfg, err, sizeof(err));

    if (!m_modem)
    {
        m_error = QString::fromUtf8(err);
        qWarning("VARAEngine::create: %s", err);
        return;
    }

    m_txLinked = false; // the new modem doesn't know about the link yet
    m_flags = 0;
    xara_force_ptt_off(m_modem, true);
    qInfo("VARAEngine::create: %s %s on %s:%u",
        qPrintable(getModeName(m_config.m_mode)), call.constData(), address.constData(), m_config.m_cmdPort);
}

void VARAEngine::destroy()
{
    if (std::shared_ptr<VARATxLink> link = m_txLink.lock())
    {
        link->push(m_out.data(), 0, false); // releases PTT
        link->setEngine(false, m_config.m_mode);
    }

    m_txLink.reset();

    if (m_modem)
    {
        xara_destroy(m_modem);
        m_modem = nullptr;
    }

    m_flags = 0;
}

void VARAEngine::process(const float *in, int n)
{
    for (int i = 0; i < n; i++)
    {
        m_in[m_inFill++] = in[i];

        if (m_inFill == m_blockSize)
        {
            m_inFill = 0;
            processBlock();
        }
    }
}

void VARAEngine::processBlock()
{
    QMutexLocker lock(&m_mutex);
    CurrentEngine current(this);

    if (!m_modem) {
        return;
    }

    // Find the Mod's link each block, so it can come and go
    std::shared_ptr<VARATxLink> link = VARATxLink::find(m_config.m_cmdPort);

    std::shared_ptr<VARATxLink> previous = m_txLink.lock();

    if (link != previous)
    {
        if (previous) {
            previous->setEngine(false, m_config.m_mode);
        }

        m_txLink = link;

        if (link) {
            link->setEngine(true, m_config.m_mode);
        }
    }

    if (m_txLinked != (link != nullptr))
    {
        m_txLinked = link != nullptr;
        xara_force_ptt_off(m_modem, !m_txLinked);
    }

    qint64 txLatency = link ? link->getTxLatency() : 0;

    if (txLatency != m_txLatency)
    {
        m_txLatency = txLatency;
        xara_set_tx_latency(m_modem, m_txLatency, m_rxLag);
    }

    m_flags = xara_process(m_modem, m_in.data(), m_out.data(), m_blockSize);

    if (link) {
        link->push(m_out.data(), m_blockSize, (m_flags & XARA_KEYED) != 0);
    }

    if (m_flags & (XARA_RESTART | XARA_ERROR))
    {
        qWarning("VARAEngine::processBlock: %s, recreating the modem",
            (m_flags & XARA_ERROR) ? "internal error" : "restart requested");
        destroy();
        create();
    }
}

VARAEngine::Status VARAEngine::getStatus() const
{
    QMutexLocker lock(&m_mutex);
    Status status;
    status.m_error = m_error;
    status.m_txLinked = m_txLinked;

    if (!m_modem) {
        return status;
    }

    xara_status st;
    xara_get_status(m_modem, &st);
    status.m_running = true;
    status.m_state = QString::fromUtf8(st.state);
    status.m_peer = QString::fromUtf8(st.peer);
    status.m_bandwidth = QString::fromUtf8(st.bw);
    status.m_level = st.level;
    status.m_attempts = st.attempts;
    status.m_passbandLow = st.passband_lo;
    status.m_passbandHigh = st.passband_hi;
    status.m_afcHz = st.afc_hz;
    status.m_cmdPort = st.cmd_port;
    status.m_dataPort = st.data_port;
    status.m_kissPort = st.kiss_port;
    status.m_p2p = st.p2p;
    status.m_keyed = (m_flags & XARA_KEYED) != 0;
    status.m_busy = (m_flags & XARA_BUSY) != 0;
    status.m_tuning = (m_flags & XARA_TUNING) != 0;
    return status;
}

QString VARAEngine::getModeName(int mode)
{
    switch (mode)
    {
    case HF500: return "HF 500";
    case HF2300: return "HF 2300";
    case HF2750: return "HF 2750";
    case FMNarrow: return "FM narrow";
    case FMWide: return "FM wide";
    default: return "?";
    }
}

QString VARAEngine::getVersion()
{
    return QString::fromUtf8(xara_version());
}
