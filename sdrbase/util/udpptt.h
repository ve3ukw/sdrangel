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

#ifndef INCLUDE_UTIL_UDPPTT_H
#define INCLUDE_UTIL_UDPPTT_H

#include <atomic>

#include <QObject>
#include <QString>
#include <QThread>

#include "export.h"

class UDPPTTWorker;

// Keys a PTT sequencer that takes text commands over UDP, such as pttseq on a PlutoSky:
// "PTT 1" keys and is repeated as a heartbeat while keyed (the sequencer releases if the
// heartbeat stops), "PTT 0" releases and "STATUS" asks for the sequencer state.
// The socket runs on its own thread, so setPtt() can be called from a DSP thread. An optional
// tail holds PTT for a while after release, and a key during the tail just cancels the release.
class SDRBASE_API UDPPTT : public QObject
{
    Q_OBJECT
public:
    explicit UDPPTT(QObject *parent = nullptr);
    ~UDPPTT() override;

    void setDestination(const QString& host, quint16 port); //!< Any thread
    void setPtt(bool on);                                   //!< Any thread. Only sends on a change
    void setTailMs(int tailMs);                             //!< Any thread. Delay before "PTT 0" is sent
    void requestStatus();                                   //!< Any thread
    bool getPtt() const { return m_ptt; }

    static const int m_heartbeatMs = 500;

signals:
    void replyReceived(const QString& reply);       //!< A datagram from the sequencer
    void error(const QString& message);

private:
    QThread m_thread;
    UDPPTTWorker *m_worker;
    std::atomic<bool> m_ptt;
};

#endif // INCLUDE_UTIL_UDPPTT_H
