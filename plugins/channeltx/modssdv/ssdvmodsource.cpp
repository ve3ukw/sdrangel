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

#include "ssdvmodreport.h"
#include "ssdvmodsource.h"

SSDVModSource::SSDVModSource() :
    m_channelSampleRate(SSDVModSettings::m_samplesPerSymbol * 9600),
    m_channelFrequencyOffset(0),
    m_modSampleRate(SSDVModSettings::m_samplesPerSymbol * 9600),
    m_channel(nullptr),
    m_linearGain(1.0f),
    m_modSample(0.0f, 0.0f),
    m_interpolatorDistance(1.0f),
    m_interpolatorDistanceRemain(0.0f),
    m_transmitting(false),
    m_symbolIndex(0),
    m_progressIndex(0),
    m_sampleIdx(0),
    m_tail(0),
    m_magsq(0.0),
    m_levelCalcCount(0),
    m_rmsLevel(0.0),
    m_peakLevelOut(0.0),
    m_peakLevel(0.0f),
    m_levelSum(0.0f),
    m_messageQueueToChannel(nullptr)
{
    applySettings(QStringList(), m_settings, true);
    applyChannelSettings(m_channelSampleRate, m_channelFrequencyOffset, true);
}

SSDVModSource::~SSDVModSource()
{
}

void SSDVModSource::pull(SampleVector::iterator begin, unsigned int nbSamples)
{
    std::for_each(
        begin,
        begin + nbSamples,
        [this](Sample& s) {
            pullOne(s);
        }
    );
}

void SSDVModSource::pullOne(Sample& sample)
{
    if (m_settings.m_channelMute)
    {
        sample.m_real = 0.0f;
        sample.m_imag = 0.0f;
        return;
    }

    Complex ci;

    if (m_interpolatorDistance > 1.0f) // decimate
    {
        modulateSample();

        while (!m_interpolator.decimate(&m_interpolatorDistanceRemain, m_modSample, &ci)) {
            modulateSample();
        }
    }
    else
    {
        if (m_interpolator.interpolate(&m_interpolatorDistanceRemain, m_modSample, &ci)) {
            modulateSample();
        }
    }

    m_interpolatorDistanceRemain += m_interpolatorDistance;

    ci *= m_carrierNco.nextIQ(); // shift to carrier frequency

    double magsq = std::norm(ci);
    magsq /= (SDR_TX_SCALED*SDR_TX_SCALED);
    m_movingAverage(magsq);
    m_magsq = m_movingAverage.asDouble();

    // Pulse shaping peaks can exceed the gain; clip rather than wrap
    const Real limit = SDR_TX_SCALEF - 1.0f;
    sample.m_real = (FixReal) std::max(-limit, std::min(limit, ci.real()));
    sample.m_imag = (FixReal) std::max(-limit, std::min(limit, ci.imag()));
}

void SSDVModSource::modulateSample()
{
    Real input = 0.0f;

    if (m_transmitting)
    {
        if (m_symbolIndex < (int) m_symbols.size())
        {
            if (m_sampleIdx == 0)
            {
                input = m_symbols[m_symbolIndex] ? -1.0f : 1.0f;

                // Report packets sent as each frame ends
                while ((m_progressIndex < (int) m_progress.size()) && (m_symbolIndex >= m_progress[m_progressIndex].first))
                {
                    if (m_messageQueueToChannel)
                    {
                        m_messageQueueToChannel->push(SSDVModReport::MsgProgress::create(
                            m_progress[m_progressIndex].second, m_progress.back().second, false));
                    }

                    m_progressIndex++;
                }
            }

            m_sampleIdx++;

            if (m_sampleIdx >= SSDVModSettings::m_samplesPerSymbol)
            {
                m_sampleIdx = 0;
                m_symbolIndex++;
            }
        }
        else if (m_tail > 0)
        {
            m_tail--;
        }
        else
        {
            m_transmitting = false;
            m_symbols.clear();

            if (m_messageQueueToChannel)
            {
                int count = m_progress.empty() ? 0 : m_progress.back().second;
                m_messageQueueToChannel->push(SSDVModReport::MsgProgress::create(count, count, true));
            }
        }
    }

    Real s = m_pulseShape.filter(input) * m_linearGain * SDR_TX_SCALEF;
    m_modSample = Complex(s, 0.0f);
    calculateLevel(s / SDR_TX_SCALEF);
}

void SSDVModSource::transmit(const std::vector<uint8_t>& symbols, const std::vector<std::pair<int, int>>& progress)
{
    m_symbols = symbols;
    m_progress = progress;
    m_symbolIndex = 0;
    m_progressIndex = 0;
    m_sampleIdx = 0;
    m_tail = m_rrcSpan * SSDVModSettings::m_samplesPerSymbol;
    m_transmitting = true;
}

void SSDVModSource::abort()
{
    if (m_transmitting)
    {
        // Let the filter ring out
        m_symbolIndex = m_symbols.size();
        m_tail = m_rrcSpan * SSDVModSettings::m_samplesPerSymbol;
    }
}

void SSDVModSource::calculateLevel(Real sample)
{
    if (m_levelCalcCount < m_levelNbSamples)
    {
        m_peakLevel = std::max(m_peakLevel, std::fabs(sample));
        m_levelSum += sample * sample;
        m_levelCalcCount++;
    }
    else
    {
        m_rmsLevel = sqrt(m_levelSum / m_levelNbSamples);
        m_peakLevelOut = m_peakLevel;
        m_peakLevel = 0.0f;
        m_levelSum = 0.0f;
        m_levelCalcCount = 0;
    }
}

void SSDVModSource::createInterpolator()
{
    Real cutoff = m_settings.m_baudRate * (1.0f + m_settings.m_rrcRolloff) / 2.0f;
    m_interpolatorDistance = (Real) m_modSampleRate / (Real) m_channelSampleRate;
    m_interpolatorDistanceRemain = 0;
    m_interpolator.create(48, m_modSampleRate, std::min(cutoff * 1.1f, m_modSampleRate / 2.2f), 3.0);
}

void SSDVModSource::applySettings(const QStringList& settingsKeys, const SSDVModSettings& settings, bool force)
{
    if ((settingsKeys.contains("baudRate") && (settings.m_baudRate != m_settings.m_baudRate))
        || (settingsKeys.contains("rrcRolloff") && (settings.m_rrcRolloff != m_settings.m_rrcRolloff))
        || force)
    {
        m_pulseShape.create(settings.m_rrcRolloff, m_rrcSpan, SSDVModSettings::m_samplesPerSymbol, RootRaisedCosine<Real>::Amplitude);
        m_modSampleRate = settings.m_baudRate * SSDVModSettings::m_samplesPerSymbol;
    }

    if (settingsKeys.contains("gain") || force) {
        m_linearGain = std::pow(10.0f, settings.m_gain / 20.0f);
    }

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    if (settingsKeys.contains("baudRate") || settingsKeys.contains("rrcRolloff") || force) {
        createInterpolator();
    }
}

void SSDVModSource::applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force)
{
    qDebug() << "SSDVModSource::applyChannelSettings:"
            << " channelSampleRate: " << channelSampleRate
            << " channelFrequencyOffset: " << channelFrequencyOffset;

    if ((channelFrequencyOffset != m_channelFrequencyOffset)
     || (channelSampleRate != m_channelSampleRate) || force)
    {
        m_carrierNco.setFreq(channelFrequencyOffset, channelSampleRate);
    }

    bool rateChanged = (m_channelSampleRate != channelSampleRate) || force;
    m_channelSampleRate = channelSampleRate;
    m_channelFrequencyOffset = channelFrequencyOffset;

    if (rateChanged) {
        createInterpolator();
    }
}
