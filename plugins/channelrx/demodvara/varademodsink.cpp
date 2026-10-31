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

#include "dsp/misc.h"
#include "maincore.h"

#include "varaengine.h"
#include "varademodsink.h"

const unsigned VARADemodSink::m_fftFilterLength = 1024;
const Real VARADemodSink::m_hfLowCutoff = 100.0f;
const Real VARADemodSink::m_hfHighCutoff = 3100.0f;
const Real VARADemodSink::m_agcTarget = 0.25f; // -12 dBFS RMS, leaving room for the modem's peaks

VARADemodSink::VARADemodSink() :
    m_channelSampleRate(VARAEngine::m_sampleRate),
    m_channelFrequencyOffset(0),
    m_channel(nullptr),
    m_engine(nullptr),
    m_rfFilter(m_fftFilterLength),
    m_interpolatorDistance(1.0f),
    m_interpolatorDistanceRemain(1.0f),
    m_rxGain(1.0f),
    m_agcPower(m_agcTarget * m_agcTarget),
    m_modemBuffer(VARAEngine::m_blockSize, 0.0f),
    m_modemBufferFill(0),
    m_modemLevelSum(0.0),
    m_modemLevelPeak(0.0f),
    m_modemLevelCount(0),
    m_audioSampleRate(VARAEngine::m_sampleRate),
    m_audioInterpolatorDistance(1.0f),
    m_audioInterpolatorDistanceRemain(1.0f),
    m_audioBufferFill(0),
    m_audioFifo(48000),
    m_magsq(0.0),
    m_magsqSum(0.0),
    m_magsqPeak(0.0),
    m_magsqCount(0)
{
    m_audioBuffer.resize(1<<12);
    applySettings(QStringList(), m_settings, true);
    applyChannelSettings(m_channelSampleRate, m_channelFrequencyOffset, true);
    applyAudioSampleRate(m_audioSampleRate);
}

Real VARADemodSink::getRFBandwidth(int mode)
{
    switch (mode)
    {
    case VARAEngine::FMNarrow: return 13000.0f;
    case VARAEngine::FMWide: return 24000.0f;
    default: return m_hfHighCutoff - m_hfLowCutoff;
    }
}

void VARADemodSink::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end)
{
    if (m_channelSampleRate == 0) {
        return;
    }

    for (SampleVector::const_iterator it = begin; it != end; ++it)
    {
        Complex c(it->real(), it->imag());
        c *= m_nco.nextIQ();

        fftfilt::cmplx *rf;
        int rfOut = m_rfFilter.runFilt(c, &rf);

        for (int i = 0; i < rfOut; i++)
        {
            Complex ci;

            if (m_interpolatorDistance == 1.0f)
            {
                processOneSample(rf[i]);
            }
            else if (m_interpolatorDistance < 1.0f) // interpolate
            {
                while (!m_interpolator.interpolate(&m_interpolatorDistanceRemain, rf[i], &ci))
                {
                    processOneSample(ci);
                    m_interpolatorDistanceRemain += m_interpolatorDistance;
                }
            }
            else // decimate
            {
                if (m_interpolator.decimate(&m_interpolatorDistanceRemain, rf[i], &ci))
                {
                    processOneSample(ci);
                    m_interpolatorDistanceRemain += m_interpolatorDistance;
                }
            }
        }
    }
}

void VARADemodSink::processOneSample(const Complex& ci)
{
    double magsq = (ci.real() * ci.real() + ci.imag() * ci.imag()) / (SDR_RX_SCALED*SDR_RX_SCALED);
    m_magsqSum += magsq;
    m_magsqPeak = std::max<double>(magsq, m_magsqPeak);
    m_magsqCount++;

    Real sample;

    if (VARAEngine::isFM(m_settings.m_mode))
    {
        double magsqRaw;
        Real deviation;
        sample = m_phaseDiscri.phaseDiscriminatorDelta(ci, magsqRaw, deviation);
    }
    else
    {
        // The filter only passed the upper sideband, so the real part is the USB audio
        sample = ci.real() / SDR_RX_SCALEF;

        if (m_settings.m_agc)
        {
            // Fast attack, slow (1 s) decay, as a radio's AGC
            Real power = sample * sample;
            Real alpha = power > m_agcPower ? 1.0f / 480.0f : 1.0f / 48000.0f;
            m_agcPower += alpha * (power - m_agcPower);
            sample *= std::min(m_agcTarget / std::sqrt(m_agcPower + 1e-12f), 1e4f);
        }
        else
        {
            sample *= m_rxGain;
        }
    }

    sample = clamp<Real>(sample, -1.0f, 1.0f);
    m_modemLevelSum += sample * sample;
    m_modemLevelPeak = std::max(m_modemLevelPeak, std::fabs(sample));
    m_modemLevelCount++;
    m_modemBuffer[m_modemBufferFill++] = sample;

    if (m_modemBufferFill == (int) m_modemBuffer.size())
    {
        if (m_engine) {
            m_engine->process(m_modemBuffer.data(), m_modemBufferFill);
        }

        m_modemBufferFill = 0;
    }

    pushAudio(m_settings.m_audioMute ? 0.0f : sample * m_settings.m_volume);
}

void VARADemodSink::pushAudio(Real sample)
{
    // The modem runs at 48 kHz, whatever the rate of the monitor audio device
    Complex in(sample, 0.0f);
    Complex ci;

    if (m_audioInterpolatorDistance == 1.0f)
    {
        writeAudio(sample);
    }
    else if (m_audioInterpolatorDistance < 1.0f) // interpolate
    {
        while (!m_audioInterpolator.interpolate(&m_audioInterpolatorDistanceRemain, in, &ci))
        {
            writeAudio(ci.real());
            m_audioInterpolatorDistanceRemain += m_audioInterpolatorDistance;
        }
    }
    else // decimate
    {
        if (m_audioInterpolator.decimate(&m_audioInterpolatorDistanceRemain, in, &ci))
        {
            writeAudio(ci.real());
            m_audioInterpolatorDistanceRemain += m_audioInterpolatorDistance;
        }
    }
}

void VARADemodSink::writeAudio(Real sample)
{
    qint16 s = clamp<float>(std::rint(sample * std::numeric_limits<int16_t>::max()),
        std::numeric_limits<int16_t>::lowest(), std::numeric_limits<int16_t>::max());
    m_audioBuffer[m_audioBufferFill].l = s;
    m_audioBuffer[m_audioBufferFill].r = s;
    ++m_audioBufferFill;

    if (m_audioBufferFill >= m_audioBuffer.size())
    {
        std::size_t res = m_audioFifo.write((const quint8*)&m_audioBuffer[0], m_audioBufferFill);

        if (res != m_audioBufferFill) {
            qDebug("VARADemodSink::writeAudio: %lu/%lu audio samples written", res, m_audioBufferFill);
        }

        m_audioBufferFill = 0;
    }
}

void VARADemodSink::getModemLevels(float& rms, float& peak)
{
    rms = m_modemLevelCount > 0 ? std::sqrt(m_modemLevelSum / m_modemLevelCount) : 0.0f;
    peak = m_modemLevelPeak;
    m_modemLevelSum = 0.0;
    m_modemLevelPeak = 0.0f;
    m_modemLevelCount = 0;
}

void VARADemodSink::createFilters()
{
    Real fs = m_channelSampleRate;

    if (VARAEngine::isFM(m_settings.m_mode))
    {
        Real cutoff = getRFBandwidth(m_settings.m_mode) / 2.0f;
        m_rfFilter.create_filter(-cutoff / fs, cutoff / fs);
        m_interpolator.create(16, m_channelSampleRate, std::min(cutoff * 1.1f, 23000.0f));
    }
    else
    {
        m_rfFilter.create_filter(m_hfLowCutoff / fs, m_hfHighCutoff / fs);
        m_interpolator.create(16, m_channelSampleRate, m_hfHighCutoff * 1.3f);
    }

    m_interpolatorDistance = Real(m_channelSampleRate) / Real(VARAEngine::m_sampleRate);
    m_interpolatorDistanceRemain = m_interpolatorDistance;
}

void VARADemodSink::applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force)
{
    qDebug() << "VARADemodSink::applyChannelSettings:"
            << " channelSampleRate: " << channelSampleRate
            << " channelFrequencyOffset: " << channelFrequencyOffset;

    const bool rateChanged = (channelSampleRate != m_channelSampleRate) || force;

    if ((channelFrequencyOffset != m_channelFrequencyOffset) || rateChanged) {
        m_nco.setFreq(-channelFrequencyOffset, channelSampleRate);
    }

    m_channelSampleRate = channelSampleRate;
    m_channelFrequencyOffset = channelFrequencyOffset;

    if (rateChanged) {
        createFilters();
    }
}

void VARADemodSink::applySettings(const QStringList& settingsKeys, const VARADemodSettings& settings, bool force)
{
    qDebug() << "VARADemodSink::applySettings:" << settings.getDebugString(settingsKeys, force);

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    if (settingsKeys.contains("mode") || force) {
        createFilters();
    }

    if (settingsKeys.contains("fmDeviation") || force) {
        m_phaseDiscri.setFMScaling(Real(VARAEngine::m_sampleRate) / (2.0f * m_settings.m_fmDeviation));
    }

    if (settingsKeys.contains("rxGainDb") || force) {
        m_rxGain = std::pow(10.0f, m_settings.m_rxGainDb / 20.0f);
    }
}

void VARADemodSink::applyAudioSampleRate(int sampleRate)
{
    qDebug("VARADemodSink::applyAudioSampleRate: %d", sampleRate);

    if (sampleRate <= 0) {
        return;
    }

    m_audioFifo.setSize(sampleRate);
    m_audioInterpolator.create(16, VARAEngine::m_sampleRate, 0.45 * std::min(sampleRate, VARAEngine::m_sampleRate));
    m_audioInterpolatorDistance = Real(VARAEngine::m_sampleRate) / Real(sampleRate);
    m_audioInterpolatorDistanceRemain = m_audioInterpolatorDistance;
    m_audioSampleRate = sampleRate;

    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(m_channel, "reportdemod", pipes);

    for (const auto& pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);
        MainCore::MsgChannelDemodReport *msg = MainCore::MsgChannelDemodReport::create(m_channel, sampleRate);
        messageQueue->push(msg);
    }
}
