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

#ifndef INCLUDE_SSDVFSKDEMOD_H
#define INCLUDE_SSDVFSKDEMOD_H

#include <array>

#include "dsp/dsptypes.h"

// Non-coherent 2-FSK (and GMSK) demodulator for a fixed number of samples per symbol:
// FM discriminator, removal of the mean frequency (AFC), integration over a symbol and Gardner symbol timing.
// Symbols are positive for the higher tone, about 1 for the nominal deviation.
class SSDVFSKDemod
{
public:
    SSDVFSKDemod();
    // deviation is the frequency of each tone from the centre, in cycles per sample
    void init(int samplesPerSymbol, float deviation, bool afc);
    void reset();

    // Returns true and sets symbol (real part) when a symbol is ready
    bool process(const Complex& sample, Complex& symbol);

    bool isLocked() const { return m_locked; }
    // Frequency offset removed by the AFC, in cycles per sample
    float getFrequencyOffset() const { return m_dc * m_deviation; }
    float getEvm() const { return m_evm; } // Mean square error of the symbol magnitude, relative to the nominal deviation

private:
    static constexpr int m_bufferSize = 64;
    static constexpr float m_lockThreshold = 0.3f;
    static constexpr float m_unlockThreshold = 0.5f;

    int m_samplesPerSymbol;
    float m_deviation;
    bool m_afc;

    Complex m_prevSample;
    float m_dc;            // Mean of the discriminator output, relative to the deviation
    std::array<float, m_bufferSize> m_integrator; // Discriminator outputs of the last symbol
    int m_integratorIndex;
    float m_integratorSum;

    // Symbol timing
    std::array<float, m_bufferSize> m_buffer;
    int m_bufferIndex;
    float m_strobe;      // Samples until the next symbol
    float m_timingIntegrator;
    float m_timingKp;
    float m_timingKi;
    float m_prevSymbol;

    bool m_locked;
    float m_evm;         // Mean square error of the symbol magnitude

    float interpolate(float delay) const; // delay in samples before the newest
};

#endif // INCLUDE_SSDVFSKDEMOD_H
