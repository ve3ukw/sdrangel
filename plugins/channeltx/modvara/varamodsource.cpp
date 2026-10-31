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

#include <QDebug>

#include "varaengine.h"
#include "varatxlink.h"
#include "varamodsource.h"

const unsigned VARAModSource::m_ssbFftLen = 1024;
const int VARAModSource::m_levelNbSamples = 480; // every 10ms

VARAModSource::VARAModSource() :
    m_channelSampleRate(VARAEngine::m_sampleRate),
    m_channelFrequencyOffset(0),
    m_channel(nullptr),
    m_modemBuffer(VARAEngine::m_blockSize, 0.0f),
    m_modemBufferPos(VARAEngine::m_blockSize),
    m_fm(true),
    m_interpolatorDistance(1.0f),
    m_interpolatorDistanceRemain(0.0f),
    m_modPhasor(0.0f),
    m_gain(1.0f),
    m_ssbBufferPos(0),
    m_magsq(0.0),
    m_levelCalcCount(0),
    m_rmsLevel(0.0),
    m_peakLevelOut(0.0),
    m_peakLevel(0.0f),
    m_levelSum(0.0f)
{
    // The modem's passband is within 100-3100 Hz in all the HF modes
    m_ssbFilter = new fftfilt(100.0f / VARAEngine::m_sampleRate, 3100.0f / VARAEngine::m_sampleRate, m_ssbFftLen);
    applySettings(QStringList(), m_settings, true);
    applyChannelSettings(m_channelSampleRate, m_channelFrequencyOffset, true);
}

VARAModSource::~VARAModSource()
{
    delete m_ssbFilter;
}

void VARAModSource::setTxLink(std::shared_ptr<VARATxLink> link)
{
    QMutexLocker lock(&m_linkMutex);
    m_txLink = link;
}

void VARAModSource::pull(SampleVector::iterator begin, unsigned int nbSamples)
{
    std::for_each(
        begin,
        begin + nbSamples,
        [this](Sample& s) {
            pullOne(s);
        }
    );
}

void VARAModSource::pullOne(Sample& sample)
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

    double magsq = ci.real() * ci.real() + ci.imag() * ci.imag();
    magsq /= (SDR_TX_SCALED*SDR_TX_SCALED);
    m_magsq = 0.99 * m_magsq + 0.01 * magsq; // moving average

    sample.m_real = (FixReal) ci.real();
    sample.m_imag = (FixReal) ci.imag();
}

float VARAModSource::nextModemSample()
{
    if (m_modemBufferPos == (int) m_modemBuffer.size())
    {
        QMutexLocker lock(&m_linkMutex);

        if (m_txLink)
        {
            m_txLink->pull(m_modemBuffer.data(), m_modemBuffer.size());
            m_fm = VARAEngine::isFM(m_txLink->getEngineMode());
        }
        else
        {
            std::fill(m_modemBuffer.begin(), m_modemBuffer.end(), 0.0f);
        }

        m_modemBufferPos = 0;
    }

    return m_modemBuffer[m_modemBufferPos++];
}

void VARAModSource::modulateSample()
{
    if (m_fm)
    {
        Real t = nextModemSample();
        calculateLevel(t);
        m_modPhasor += (float) ((2.0 * M_PI * m_settings.m_fmDeviation / (float) VARAEngine::m_sampleRate) * t);

        // limit phasor range to ]-pi,pi]
        if (m_modPhasor > M_PI) {
            m_modPhasor -= (float) (2.0 * M_PI);
        } else if (m_modPhasor <= -M_PI) {
            m_modPhasor += (float) (2.0 * M_PI);
        }

        m_modSample.real((float) (cos(m_modPhasor) * 0.891235351562f * SDR_TX_SCALEF * std::min(m_gain, 1.0f))); // -1 dB
        m_modSample.imag((float) (sin(m_modPhasor) * 0.891235351562f * SDR_TX_SCALEF * std::min(m_gain, 1.0f)));
    }
    else
    {
        // The filter outputs blocks of the analytic signal, so keep feeding it until it has one
        while (m_ssbBufferPos == (int) m_ssbBuffer.size())
        {
            Real t = nextModemSample();
            calculateLevel(t);
            fftfilt::cmplx *filtered;
            // Twice, as the upper sideband carries half of the power of the real signal
            int n = m_ssbFilter->runSSB(Complex(2.0f * t * m_gain * SDR_TX_SCALEF, 0.0f), &filtered, true);

            if (n > 0)
            {
                m_ssbBuffer.assign(filtered, filtered + n);
                m_ssbBufferPos = 0;
            }
        }

        m_modSample = m_ssbBuffer[m_ssbBufferPos++];
    }
}

void VARAModSource::calculateLevel(Real sample)
{
    if (m_levelCalcCount < (quint32) m_levelNbSamples)
    {
        m_peakLevel = std::max(std::fabs(m_peakLevel), std::fabs(sample));
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

void VARAModSource::applySettings(const QStringList& settingsKeys, const VARAModSettings& settings, bool force)
{
    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    if (settingsKeys.contains("gain") || force) {
        m_gain = std::pow(10.0f, m_settings.m_gain / 20.0f);
    }
}

void VARAModSource::applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force)
{
    qDebug() << "VARAModSource::applyChannelSettings:"
            << " channelSampleRate: " << channelSampleRate
            << " channelFrequencyOffset: " << channelFrequencyOffset;

    if ((channelFrequencyOffset != m_channelFrequencyOffset)
     || (channelSampleRate != m_channelSampleRate) || force)
    {
        m_carrierNco.setFreq(channelFrequencyOffset, channelSampleRate);
    }

    if ((channelSampleRate != m_channelSampleRate) || force)
    {
        m_interpolatorDistanceRemain = 0;
        m_interpolatorDistance = (Real) VARAEngine::m_sampleRate / (Real) channelSampleRate;
        // Wide enough for VARA FM wide, whose deviation and audio together reach about 11 kHz
        m_interpolator.create(48, VARAEngine::m_sampleRate, 14000.0, 3.0);
    }

    m_channelSampleRate = channelSampleRate;
    m_channelFrequencyOffset = channelFrequencyOffset;
}
