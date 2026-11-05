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

#ifndef INCLUDE_SSDVBPSKDEMOD_H
#define INCLUDE_SSDVBPSKDEMOD_H

#include <array>

#include "dsp/dsptypes.h"
#include "dsp/rootraisedcosine.h"

// BPSK demodulator for a fixed number of samples per symbol:
// FLL on the squared signal, matched RRC filter, AGC, Gardner symbol timing and a Costas loop at the symbol rate
class SSDVBPSKDemod
{
public:
    SSDVBPSKDemod();
    void init(int samplesPerSymbol, float rrcRolloff, float pllBandwidth, bool afc);
    void reset();

    // Returns true and sets symbol (derotated, about unit amplitude) when a symbol is ready
    bool process(const Complex& sample, Complex& symbol);

    bool isLocked() const { return m_locked; }
    float getLock() const { return m_lock; }
    // Frequency offset corrected by the FLL and Costas loop, in cycles per sample
    float getFrequencyOffset() const;
    float getEvm() const { return m_evm; } // RMS error vector relative to symbol amplitude

private:
    static constexpr int m_bufferSize = 64;
    static constexpr float m_lockThreshold = 0.25f;
    static constexpr float m_unlockThreshold = 0.1f;

    int m_samplesPerSymbol;
    bool m_afc;

    // FLL
    float m_fllFreq;   // radians per sample
    float m_fllPhase;
    Complex m_fllAverage;

    RootRaisedCosine<Real> m_rrcI;
    RootRaisedCosine<Real> m_rrcQ;

    float m_agcLevel;

    // Symbol timing
    std::array<Complex, m_bufferSize> m_buffer;
    int m_bufferIndex;
    float m_strobe;      // Samples until the next symbol
    float m_timingIntegrator;
    float m_timingKp;
    float m_timingKi;
    Complex m_prevSymbol;

    // Costas loop
    float m_phase;
    float m_freq;        // radians per symbol
    float m_alpha;
    float m_beta;

    float m_lock;
    bool m_locked;
    float m_evm;

    Complex interpolate(float delay) const; // delay in samples before the newest
};

#endif // INCLUDE_SSDVBPSKDEMOD_H
