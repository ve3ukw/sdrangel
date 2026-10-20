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

#include "dsp/dspengine.h"
#include "dsp/dspcommands.h"

#include "sstvdemodbaseband.h"

MESSAGE_CLASS_DEFINITION(SSTVDemodBaseband::MsgConfigureSSTVDemodBaseband, Message)
MESSAGE_CLASS_DEFINITION(SSTVDemodBaseband::MsgResetDecoder, Message)

SSTVDemodBaseband::SSTVDemodBaseband() :
    m_channelizer(&m_sink)
{
    m_sampleFifo.setSize(SampleSinkFifo::getSizePolicy(48000));

    qDebug("SSTVDemodBaseband::SSTVDemodBaseband");
    QObject::connect(
        &m_sampleFifo,
        &SampleSinkFifo::dataReady,
        this,
        &SSTVDemodBaseband::handleData,
        Qt::QueuedConnection
    );

    DSPEngine::instance()->getAudioDeviceManager()->addAudioSink(m_sink.getAudioFifo(), getInputMessageQueue());
    m_sink.applyAudioSampleRate(DSPEngine::instance()->getAudioDeviceManager()->getOutputSampleRate());
    m_channelSampleRate = 0;

    QObject::connect(
        &m_inputMessageQueue,
        &MessageQueue::messageEnqueued,
        this,
        &SSTVDemodBaseband::handleInputMessages,
        Qt::QueuedConnection
    );
}

SSTVDemodBaseband::~SSTVDemodBaseband()
{
    DSPEngine::instance()->getAudioDeviceManager()->removeAudioSink(m_sink.getAudioFifo());
}

void SSTVDemodBaseband::reset()
{
    QMutexLocker mutexLocker(&m_mutex);
    m_sampleFifo.reset();
    m_channelSampleRate = 0;
}

void SSTVDemodBaseband::setChannel(ChannelAPI *channel)
{
    m_sink.setChannel(channel);
}

void SSTVDemodBaseband::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end)
{
    m_sampleFifo.write(begin, end);
}

void SSTVDemodBaseband::handleData()
{
    QMutexLocker mutexLocker(&m_mutex);

    while ((m_sampleFifo.fill() > 0) && (m_inputMessageQueue.size() == 0))
    {
        SampleVector::iterator part1begin;
        SampleVector::iterator part1end;
        SampleVector::iterator part2begin;
        SampleVector::iterator part2end;

        std::size_t count = m_sampleFifo.readBegin(m_sampleFifo.fill(), &part1begin, &part1end, &part2begin, &part2end);

        // first part of FIFO data
        if (part1begin != part1end) {
            m_channelizer.feed(part1begin, part1end);
        }

        // second part of FIFO data (used when block wraps around)
        if(part2begin != part2end) {
            m_channelizer.feed(part2begin, part2end);
        }

        m_sampleFifo.readCommit((unsigned int) count);
    }
}

void SSTVDemodBaseband::handleInputMessages()
{
    Message* message;

    while ((message = m_inputMessageQueue.pop()))
    {
        if (!handleMessage(*message)) {
            qDebug("%s: unhandled message: %s", Q_FUNC_INFO, message->getIdentifier());
        }
        delete message;
    }
}

bool SSTVDemodBaseband::handleMessage(const Message& cmd)
{
    if (MsgConfigureSSTVDemodBaseband::match(cmd))
    {
        QMutexLocker mutexLocker(&m_mutex);
        MsgConfigureSSTVDemodBaseband& cfg = (MsgConfigureSSTVDemodBaseband&) cmd;
        qDebug() << "SSTVDemodBaseband::handleMessage: MsgConfigureSSTVDemodBaseband";

        applySettings(cfg.getSettingsKeys(), cfg.getSettings(), cfg.getForce());

        return true;
    }
    else if (MsgResetDecoder::match(cmd))
    {
        QMutexLocker mutexLocker(&m_mutex);
        m_sink.resetDecoder();

        return true;
    }
    else if (DSPSignalNotification::match(cmd))
    {
        QMutexLocker mutexLocker(&m_mutex);
        DSPSignalNotification& notif = (DSPSignalNotification&) cmd;
        qDebug() << "SSTVDemodBaseband::handleMessage: DSPSignalNotification: basebandSampleRate: " << notif.getSampleRate();
        m_sampleFifo.setSize(SampleSinkFifo::getSizePolicy(notif.getSampleRate()));
        m_channelizer.setBasebandSampleRate(notif.getSampleRate());
        m_sink.applyChannelSettings(m_channelizer.getChannelSampleRate(), m_channelizer.getChannelFrequencyOffset());

        if (m_channelSampleRate != m_channelizer.getChannelSampleRate())
        {
            m_sink.applyAudioSampleRate(m_sink.getAudioSampleRate()); // reapply when channel sample rate changes
            m_channelSampleRate = m_channelizer.getChannelSampleRate();
        }

        return true;
    }
    else
    {
        return false;
    }
}

void SSTVDemodBaseband::applySettings(const QStringList& settingsKeys, const SSTVDemodSettings& settings, const bool force)
{
    if ((settingsKeys.contains("inputFrequencyOffset") && (settings.m_inputFrequencyOffset != m_settings.m_inputFrequencyOffset)) || force)
    {
        m_channelizer.setChannelization(m_sink.getAudioSampleRate(), settings.m_inputFrequencyOffset);
        m_sink.applyChannelSettings(m_channelizer.getChannelSampleRate(), m_channelizer.getChannelFrequencyOffset());

        if (m_channelSampleRate != m_channelizer.getChannelSampleRate())
        {
            m_sink.applyAudioSampleRate(m_sink.getAudioSampleRate()); // reapply when channel sample rate changes
            m_channelSampleRate = m_channelizer.getChannelSampleRate();
        }
    }

    if ((settingsKeys.contains("audioDeviceName") && (settings.m_audioDeviceName != m_settings.m_audioDeviceName)) || force)
    {
        AudioDeviceManager *audioDeviceManager = DSPEngine::instance()->getAudioDeviceManager();
        int audioDeviceIndex = audioDeviceManager->getOutputDeviceIndex(settings.m_audioDeviceName);
        audioDeviceManager->removeAudioSink(m_sink.getAudioFifo());
        audioDeviceManager->addAudioSink(m_sink.getAudioFifo(), getInputMessageQueue(), audioDeviceIndex);
        int audioSampleRate = audioDeviceManager->getOutputSampleRate(audioDeviceIndex);

        if (m_sink.getAudioSampleRate() != audioSampleRate)
        {
            m_channelizer.setChannelization(audioSampleRate, settings.m_inputFrequencyOffset);
            m_sink.applyChannelSettings(m_channelizer.getChannelSampleRate(), m_channelizer.getChannelFrequencyOffset());
            m_sink.applyAudioSampleRate(audioSampleRate);
        }
    }

    m_sink.applySettings(settingsKeys, settings, force);

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }
}

int SSTVDemodBaseband::getChannelSampleRate() const
{
    return m_channelizer.getChannelSampleRate();
}


void SSTVDemodBaseband::setBasebandSampleRate(int sampleRate)
{
    m_channelizer.setBasebandSampleRate(sampleRate);
    m_sink.applyChannelSettings(m_channelizer.getChannelSampleRate(), m_channelizer.getChannelFrequencyOffset());
}
