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

#include "ssdvbpskdemod.h"

// Averaging of the FLL discriminator and FLL gain, per sample. The gain drops once the Costas loop has locked.
static constexpr float m_fllAveraging = 0.002f;
static constexpr float m_fllGain = 0.0002f;
static constexpr float m_fllLockedGain = 0.00001f;
// AGC averaging per sample
static constexpr float m_agcAveraging = 0.0005f;
// Lock detector and EVM averaging per symbol
static constexpr float m_lockAveraging = 0.005f;

SSDVBPSKDemod::SSDVBPSKDemod() :
    m_samplesPerSymbol(8),
    m_afc(true)
{
    init(8, 0.35f, 0.02f, true);
}

void SSDVBPSKDemod::init(int samplesPerSymbol, float rrcRolloff, float pllBandwidth, bool afc)
{
    m_samplesPerSymbol = samplesPerSymbol;
    m_afc = afc;
    m_rrcI.create(rrcRolloff, 8, samplesPerSymbol, RootRaisedCosine<Real>::Energy);
    m_rrcQ.create(rrcRolloff, 8, samplesPerSymbol, RootRaisedCosine<Real>::Energy);

    // Second order loops with damping 0.707
    const float zeta = 0.707f;
    float theta = pllBandwidth / (zeta + 0.25f / zeta);
    float d = 1.0f + 2.0f * zeta * theta + theta * theta;
    m_alpha = 4.0f * zeta * theta / d;
    m_beta = 4.0f * theta * theta / d;

    // Timing loop bandwidth 0.5% of the symbol rate
    float timingTheta = 0.005f / (zeta + 0.25f / zeta);
    float timingD = 1.0f + 2.0f * zeta * timingTheta + timingTheta * timingTheta;
    m_timingKp = 4.0f * zeta * timingTheta / timingD;
    m_timingKi = 4.0f * timingTheta * timingTheta / timingD;

    reset();
}

void SSDVBPSKDemod::reset()
{
    m_fllFreq = 0.0f;
    m_fllPhase = 0.0f;
    m_fllAverage = Complex(0.0f, 0.0f);
    m_agcLevel = 1e-6f;
    m_buffer.fill(Complex(0.0f, 0.0f));
    m_bufferIndex = 0;
    m_strobe = m_samplesPerSymbol;
    m_timingIntegrator = 0.0f;
    m_prevSymbol = Complex(0.0f, 0.0f);
    m_phase = 0.0f;
    m_freq = 0.0f;
    m_lock = 0.0f;
    m_locked = false;
    m_evm = 1.0f;
}

float SSDVBPSKDemod::getFrequencyOffset() const
{
    return (m_fllFreq + m_freq / m_samplesPerSymbol) / (2.0f * (float) M_PI);
}

Complex SSDVBPSKDemod::interpolate(float delay) const
{
    int i = (int) std::floor(delay);
    float mu = delay - i;
    const Complex& a = m_buffer[(m_bufferIndex - i + m_bufferSize) % m_bufferSize];
    const Complex& b = m_buffer[(m_bufferIndex - i - 1 + 2 * m_bufferSize) % m_bufferSize];

    return a * (1.0f - mu) + b * mu;
}

bool SSDVBPSKDemod::process(const Complex& sample, Complex& symbol)
{
    // Remove the frequency offset estimated by the FLL
    Complex x = sample;

    if (m_afc)
    {
        m_fllPhase += m_fllFreq;

        if (m_fllPhase > (float) M_PI) {
            m_fllPhase -= 2.0f * (float) M_PI;
        } else if (m_fllPhase < (float) -M_PI) {
            m_fllPhase += 2.0f * (float) M_PI;
        }

        x *= std::polar(1.0f, -m_fllPhase);
    }

    Complex y(m_rrcI.filter(x.real()), m_rrcQ.filter(x.imag()));

    // AGC
    float magSq = std::norm(y);
    m_agcLevel += m_agcAveraging * (magSq - m_agcLevel);
    y /= std::sqrt(std::max(m_agcLevel, 1e-12f));

    m_bufferIndex = (m_bufferIndex + 1) % m_bufferSize;
    m_buffer[m_bufferIndex] = y;

    // FLL: squaring removes the BPSK modulation, so the phase of the squared signal advances by twice the
    // offset. Comparing samples half a symbol apart gives a range of +/- a quarter of the symbol rate.
    if (m_afc)
    {
        int lag = m_samplesPerSymbol / 2;
        Complex prev = m_buffer[(m_bufferIndex - lag + m_bufferSize) % m_bufferSize];
        Complex sq = y * y;
        m_fllAverage += m_fllAveraging * (sq * std::conj(prev * prev) - m_fllAverage);
        float gain = isLocked() ? m_fllLockedGain : m_fllGain;
        m_fllFreq += gain * std::arg(m_fllAverage) / (2.0f * lag);
        // Limit to the discriminator range
        float limit = (float) M_PI / (2.0f * lag);
        m_fllFreq = std::max(-limit, std::min(limit, m_fllFreq));
    }

    m_strobe -= 1.0f;

    if (m_strobe > 0.0f) {
        return false;
    }

    // The symbol time was -m_strobe samples ago
    float delay = -m_strobe;
    Complex current = interpolate(delay);
    Complex mid = interpolate(delay + m_samplesPerSymbol / 2.0f);

    // Gardner timing error, positive when sampling late
    float error = std::real((m_prevSymbol - current) * std::conj(mid));
    error = std::max(-1.0f, std::min(1.0f, error));
    m_prevSymbol = current;
    m_timingIntegrator += m_timingKi * error;
    m_timingIntegrator = std::max(-0.1f, std::min(0.1f, m_timingIntegrator));
    m_strobe += m_samplesPerSymbol * (1.0f + m_timingKp * error + m_timingIntegrator);

    // Costas loop
    Complex z = current * std::polar(1.0f, -m_phase);
    float decision = z.real() >= 0.0f ? 1.0f : -1.0f;
    float phaseError = decision * z.imag();
    phaseError = std::max(-1.0f, std::min(1.0f, phaseError));
    m_freq += m_beta * phaseError;
    m_freq = std::max(-0.5f, std::min(0.5f, m_freq));
    m_phase += m_freq + m_alpha * phaseError;

    if (m_phase > (float) M_PI) {
        m_phase -= 2.0f * (float) M_PI;
    } else if (m_phase < (float) -M_PI) {
        m_phase += 2.0f * (float) M_PI;
    }

    // Lock: (I^2 - Q^2) / (I^2 + Q^2) averages to 1 when locked and 0 when not
    float power = std::norm(z);

    if (power > 1e-12f) {
        m_lock += m_lockAveraging * ((z.real() * z.real() - z.imag() * z.imag()) / power - m_lock);
    }

    if (m_lock > m_lockThreshold) {
        m_locked = true;
    } else if (m_lock < m_unlockThreshold) {
        m_locked = false;
    }

    float evm = std::norm(z - Complex(decision, 0.0f));
    m_evm += m_lockAveraging * (evm - m_evm);

    symbol = z;
    return true;
}
