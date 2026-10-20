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

#include "sstvdemodreport.h"
#include "sstvdemodsink.h"

const unsigned SSTVDemodSink::m_fftFilterLength = 1024;
const double SSTVDemodSink::m_audioCutoff = 3000.0;

SSTVDemodSink::SSTVDemodSink() :
    m_channelSampleRate(48000),
    m_channelFrequencyOffset(0),
    m_channel(nullptr),
    m_audioSampleRate(48000),
    m_audioBufferFill(0),
    m_audioFifo(48000),
    m_interpolatorDistance(1.0f),
    m_interpolatorDistanceRemain(1.0f),
    m_rfFilter(m_fftFilterLength),
    m_magsq(0.0),
    m_magsqSum(0.0),
    m_magsqPeak(0.0),
    m_magsqCount(0),
    m_messageQueueToChannel(nullptr)
{
    m_audioBuffer.resize(1<<14);
    m_decoder.setListener(this);
    applySettings(QStringList(), m_settings, true);
    applyChannelSettings(m_channelSampleRate, m_channelFrequencyOffset, true);
}

void SSTVDemodSink::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end)
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

void SSTVDemodSink::processOneSample(Complex& ci)
{
    double magsqRaw;
    Real deviation;
    Real demod = m_phaseDiscri.phaseDiscriminatorDelta(ci, magsqRaw, deviation);

    Real magsq = magsqRaw / (SDR_RX_SCALED*SDR_RX_SCALED);
    m_magsqSum += magsq;
    m_magsqPeak = std::max<double>(magsq, m_magsqPeak);
    m_magsqCount++;

    m_decoder.process(demod);

    qint16 sample = 0;

    if (!m_settings.m_audioMute)
    {
        Real audioSample = m_audioLowpass.filter(demod) * m_settings.m_volume * std::numeric_limits<int16_t>::max();
        sample = clamp<float>(std::rint(audioSample), std::numeric_limits<int16_t>::lowest(), std::numeric_limits<int16_t>::max());
    }

    m_audioBuffer[m_audioBufferFill].l = sample;
    m_audioBuffer[m_audioBufferFill].r = sample;
    ++m_audioBufferFill;

    if (m_audioBufferFill >= m_audioBuffer.size())
    {
        std::size_t res = m_audioFifo.write((const quint8*)&m_audioBuffer[0], m_audioBufferFill);

        if (res != m_audioBufferFill) {
            qDebug("SSTVDemodSink::processOneSample: %lu/%lu audio samples written", res, m_audioBufferFill);
        }

        m_audioBufferFill = 0;
    }
}

void SSTVDemodSink::createRFFilter()
{
    Real cutoff = m_settings.m_rfBandwidth / 2.0f;
    m_rfFilter.create_filter(-cutoff / m_channelSampleRate, cutoff / m_channelSampleRate);
}

void SSTVDemodSink::applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force)
{
    qDebug() << "SSTVDemodSink::applyChannelSettings:"
            << " channelSampleRate: " << channelSampleRate
            << " channelFrequencyOffset: " << channelFrequencyOffset;

    const bool rateChanged = (channelSampleRate != m_channelSampleRate) || force;

    if ((channelFrequencyOffset != m_channelFrequencyOffset) || rateChanged)
    {
        m_nco.setFreq(-channelFrequencyOffset, channelSampleRate);
    }

    if (rateChanged)
    {
        m_interpolator.create(16, channelSampleRate, m_settings.m_rfBandwidth / 2.2);
        m_interpolatorDistance = Real(channelSampleRate) / Real(m_audioSampleRate);
        m_interpolatorDistanceRemain = m_interpolatorDistance;
    }

    m_channelSampleRate = channelSampleRate;
    m_channelFrequencyOffset = channelFrequencyOffset;

    if (rateChanged) {
        createRFFilter();
    }
}

void SSTVDemodSink::applySettings(const QStringList& settingsKeys, const SSTVDemodSettings& settings, bool force)
{
    qDebug() << "SSTVDemodSink::applySettings:" << settings.getDebugString(settingsKeys, force);

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    if (settingsKeys.contains("rfBandwidth") || force)
    {
        m_interpolator.create(16, m_channelSampleRate, m_settings.m_rfBandwidth / 2.2);
        m_interpolatorDistance = Real(m_channelSampleRate) / Real(m_audioSampleRate);
        m_interpolatorDistanceRemain = m_interpolatorDistance;
        createRFFilter();
    }

    if (settingsKeys.contains("fmDeviation") || force) {
        m_phaseDiscri.setFMScaling(Real(m_audioSampleRate) / (2.0f * m_settings.m_fmDeviation));
    }

    if (settingsKeys.contains("autoMode") || settingsKeys.contains("mode") || force) {
        m_decoder.setMode(m_settings.m_autoMode ? -1 : m_settings.m_mode);
    }
}

void SSTVDemodSink::applyAudioSampleRate(int sampleRate)
{
    qDebug("SSTVDemodSink::applyAudioSampleRate: %d m_channelSampleRate: %d", sampleRate, m_channelSampleRate);

    m_audioLowpass.create(101, sampleRate, m_audioCutoff);
    m_phaseDiscri.setFMScaling(Real(sampleRate) / (2.0f * m_settings.m_fmDeviation));
    m_audioFifo.setSize(sampleRate);
    m_interpolatorDistance = Real(m_channelSampleRate) / Real(sampleRate);
    m_interpolatorDistanceRemain = m_interpolatorDistance;
    m_audioSampleRate = sampleRate;
    m_decoder.setSampleRate(sampleRate);

    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(m_channel, "reportdemod", pipes);

    for (const auto& pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);
        MainCore::MsgChannelDemodReport *msg = MainCore::MsgChannelDemodReport::create(m_channel, sampleRate);
        messageQueue->push(msg);
    }
}

void SSTVDemodSink::imageStarted(int modeIndex, int visCode)
{
    if (m_messageQueueToChannel) {
        m_messageQueueToChannel->push(SSTVDemodReport::MsgImageStarted::create(modeIndex, visCode));
    }
}

void SSTVDemodSink::linesDecoded(const QImage& image, int firstRow, int rowCount)
{
    if (m_messageQueueToChannel)
    {
        m_messageQueueToChannel->push(SSTVDemodReport::MsgLines::create(
            firstRow,
            image.copy(0, firstRow, image.width(), rowCount),
            m_decoder.getRowsDone(),
            m_decoder.getClockErrorPpm()));
    }
}

void SSTVDemodSink::imageFinished(const QImage& image, bool complete)
{
    if (m_messageQueueToChannel) {
        m_messageQueueToChannel->push(SSTVDemodReport::MsgImageFinished::create(image.copy(), complete));
    }
}
