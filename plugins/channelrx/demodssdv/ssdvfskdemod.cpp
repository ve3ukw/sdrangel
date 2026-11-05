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

#include <algorithm>
#include <cmath>

#include "ssdvfskdemod.h"

// Averaging of the mean frequency, in symbols: fast to pull in at the start of a burst, slow once locked
static constexpr float m_dcSymbols = 32.0f;
static constexpr float m_dcLockedSymbols = 256.0f;
// Largest offset the AFC follows, relative to the deviation
static constexpr float m_dcLimit = 2.0f;
// Discriminator outputs are clipped to this, relative to the deviation, so noise spikes don't move the AFC
static constexpr float m_discriminatorLimit = 3.0f;
// Lock detector and EVM averaging per symbol
static constexpr float m_lockAveraging = 0.02f;

SSDVFSKDemod::SSDVFSKDemod() :
    m_samplesPerSymbol(8),
    m_deviation(0.125f),
    m_afc(true)
{
    init(8, 0.125f, true);
}

void SSDVFSKDemod::init(int samplesPerSymbol, float deviation, bool afc)
{
    m_samplesPerSymbol = std::min(samplesPerSymbol, m_bufferSize / 2);
    m_deviation = std::max(deviation, 1e-4f);
    m_afc = afc;

    // Timing loop bandwidth 0.5% of the symbol rate, damping 0.707
    const float zeta = 0.707f;
    float theta = 0.005f / (zeta + 0.25f / zeta);
    float d = 1.0f + 2.0f * zeta * theta + theta * theta;
    m_timingKp = 4.0f * zeta * theta / d;
    m_timingKi = 4.0f * theta * theta / d;

    reset();
}

void SSDVFSKDemod::reset()
{
    m_prevSample = Complex(0.0f, 0.0f);
    m_dc = 0.0f;
    m_integrator.fill(0.0f);
    m_integratorIndex = 0;
    m_integratorSum = 0.0f;
    m_buffer.fill(0.0f);
    m_bufferIndex = 0;
    m_strobe = m_samplesPerSymbol;
    m_timingIntegrator = 0.0f;
    m_prevSymbol = 0.0f;
    m_locked = false;
    m_evm = 1.0f;
}

float SSDVFSKDemod::interpolate(float delay) const
{
    int i = (int) std::floor(delay);
    float mu = delay - i;
    float a = m_buffer[(m_bufferIndex - i + m_bufferSize) % m_bufferSize];
    float b = m_buffer[(m_bufferIndex - i - 1 + 2 * m_bufferSize) % m_bufferSize];

    return a * (1.0f - mu) + b * mu;
}

bool SSDVFSKDemod::process(const Complex& sample, Complex& symbol)
{
    // Instantaneous frequency, relative to the deviation
    float f = std::arg(sample * std::conj(m_prevSample)) / (2.0f * (float) M_PI * m_deviation);
    m_prevSample = sample;
    f = std::max(-m_discriminatorLimit, std::min(m_discriminatorLimit, f));

    if (m_afc)
    {
        float symbols = m_locked ? m_dcLockedSymbols : m_dcSymbols;
        m_dc += (f - m_dc) / (symbols * m_samplesPerSymbol);
        m_dc = std::max(-m_dcLimit, std::min(m_dcLimit, m_dc));
        f -= m_dc;
    }

    // Integrate over a symbol (matched filter for rectangular FSK)
    m_integratorSum += f - m_integrator[m_integratorIndex];
    m_integrator[m_integratorIndex] = f;
    m_integratorIndex = (m_integratorIndex + 1) % m_samplesPerSymbol;
    float y = m_integratorSum / m_samplesPerSymbol;

    m_bufferIndex = (m_bufferIndex + 1) % m_bufferSize;
    m_buffer[m_bufferIndex] = y;

    m_strobe -= 1.0f;

    if (m_strobe > 0.0f) {
        return false;
    }

    // The symbol time was -m_strobe samples ago
    float delay = -m_strobe;
    float current = interpolate(delay);
    float mid = interpolate(delay + m_samplesPerSymbol / 2.0f);

    // Gardner timing error, positive when sampling late
    float error = (m_prevSymbol - current) * mid;
    error = std::max(-1.0f, std::min(1.0f, error));
    m_prevSymbol = current;
    m_timingIntegrator += m_timingKi * error;
    m_timingIntegrator = std::max(-0.1f, std::min(0.1f, m_timingIntegrator));
    m_strobe += m_samplesPerSymbol * (1.0f + m_timingKp * error + m_timingIntegrator);

    // Lock when the symbols have about the nominal deviation (noise gives a wide spread)
    float e = std::abs(current) - 1.0f;
    m_evm += m_lockAveraging * (e * e - m_evm);
    float evm = std::sqrt(m_evm);

    if (evm < m_lockThreshold) {
        m_locked = true;
    } else if (evm > m_unlockThreshold) {
        m_locked = false;
    }

    symbol = Complex(current, 0.0f);
    return true;
}
