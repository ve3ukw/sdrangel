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

#ifndef INCLUDE_MODEMVARA_VARAENGINE_H
#define INCLUDE_MODEMVARA_VARAENGINE_H

#include <functional>
#include <memory>
#include <vector>

#include <QDateTime>
#include <QMutex>
#include <QString>

#include "export.h"

struct xara_modem;
class VARATxLink;

// One xara VARA modem. Its VARA command/data (and KISS) TCP ports run on xara's own threads.
//
// process() is fed with received audio at 48 kHz without gaps (xara counts time in samples),
// and the transmit audio it produces goes to the VARATxLink of a VARA Mod on the same command
// port. Without one, xara is told PTT can't be keyed, so it never thinks it transmitted.
class MODEMVARA_API VARAEngine
{
public:
    enum Mode { // As xara_mode
        HF500,
        HF2300,
        HF2750,
        FMNarrow,
        FMWide
    };

    struct Config
    {
        int m_mode = HF2300;
        QString m_call;
        QString m_address = "127.0.0.1";
        quint16 m_cmdPort = 8300; //!< Data port is cmdPort + 1
        quint16 m_kissPort = 0;   //!< 0 for none
        double m_driveDb = -5.0;
        bool m_registered = false; //!< Higher speed levels, for a call with a registered copy of VARA

        bool operator==(const Config& other) const;
        bool operator!=(const Config& other) const { return !(*this == other); }
    };

    struct Status
    {
        bool m_running = false;
        QString m_error;
        QString m_state;
        QString m_peer;
        QString m_bandwidth;
        unsigned int m_level = 0;
        unsigned int m_attempts = 0;
        double m_passbandLow = 0.0;
        double m_passbandHigh = 0.0;
        double m_afcHz = 0.0; //!< NaN when unknown
        quint16 m_cmdPort = 0;
        quint16 m_dataPort = 0;
        quint16 m_kissPort = 0;
        bool m_keyed = false;
        bool m_busy = false;
        bool m_tuning = false;
        bool m_txLinked = false;
    };

    //! Gets xara's info lines (what it shows on its console), from any thread: the lines of this modem, and
    //! those of xara's TCP host, which all modems share as xara doesn't say which one they come from.
    typedef std::function<void(const QDateTime& dateTime, const QString& line)> LogCallback;

    static const int m_sampleRate = 48000;
    static const int m_blockSize = 480;

    VARAEngine();
    ~VARAEngine();

    void setConfig(const Config& config); //!< Recreates the modem (and so its ports) when changed
    void setRxLag(int samples);           //!< Samples from RF to process() input
    void process(const float *in, int n); //!< Received audio in [-1, 1]
    Status getStatus() const;
    void setLogCallback(const LogCallback& callback);
    static void log(int level, const char *line); //!< xara's log callback

    static bool isFM(int mode) { return (mode == FMNarrow) || (mode == FMWide); }
    static QString getModeName(int mode);
    static QString getVersion();

private:
    mutable QMutex m_mutex;
    xara_modem *m_modem;
    Config m_config;
    QString m_error;
    std::vector<float> m_in;
    std::vector<float> m_out;
    int m_inFill;
    int m_rxLag;
    qint64 m_txLatency;
    bool m_txLinked;
    std::weak_ptr<VARATxLink> m_txLink; //!< Weak, so the link goes when its Mod lets go of it
    unsigned int m_flags;
    LogCallback m_logCallback; //!< Guarded by the lock of the engine list

    void create();
    void destroy();
    void processBlock();
};

#endif // INCLUDE_MODEMVARA_VARAENGINE_H
