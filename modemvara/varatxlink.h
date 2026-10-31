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

#ifndef INCLUDE_MODEMVARA_VARATXLINK_H
#define INCLUDE_MODEMVARA_VARATXLINK_H

#include <functional>
#include <memory>
#include <vector>

#include <QMutex>

#include "export.h"

// Carries the transmit audio of a VARA engine (owned by a VARA Demod, which runs xara's
// clock from the Rx stream) to a VARA Mod, which pulls it at the Tx sample rate.
//
// The Mod creates the link for an engine's command port and keeps it; the engine finds it by
// port. Either side can go away without the other noticing anything but silence.
//
// The FIFO holds the PTT lead: while unkeyed it is kept between target and target + window
// samples (by padding or dropping silence only), so the time from a key edge to its audio
// reaching the Tx stream is known, and Rx/Tx clock drift is taken up between transmissions.
class MODEMVARA_API VARATxLink
{
public:
    VARATxLink();
    ~VARATxLink();

    static std::shared_ptr<VARATxLink> create(quint16 cmdPort); //!< Mod side: replaces any link on the port
    static std::shared_ptr<VARATxLink> find(quint16 cmdPort);   //!< Engine side

    // Mod side
    void setTiming(int leadSamples, int windowSamples, int txPathSamples);
    void setPttCallback(std::function<void(bool)> callback); //!< Called from the engine's thread on key edges
    void pull(float *out, int n);                             //!< Zeros where no audio is buffered
    bool getKeyed() const;
    bool getEngineAttached() const;
    int getEngineMode() const;
    int getFill() const;
    unsigned int getUnderflows() const;

    // Engine side
    void push(const float *in, int n, bool keyed);
    qint64 getTxLatency() const; //!< Samples from push() to RF, including the PTT lead
    void setEngine(bool attached, int mode);

private:
    static const int m_capacity = 1 << 17; //!< 2.7 s at 48 kHz

    mutable QMutex m_mutex;
    std::vector<float> m_ring;
    int m_head; //!< Next write
    int m_fill;
    int m_target;
    int m_window;
    int m_txPath;
    bool m_keyed;
    bool m_engineAttached;
    int m_engineMode;
    unsigned int m_underflows;
    std::function<void(bool)> m_pttCallback;

    void write(float v);
    float read();
    float peek(int i) const; //!< i-th oldest sample
    void trimSilence();      //!< Drop leading zeros down to the target
};

#endif // INCLUDE_MODEMVARA_VARATXLINK_H
