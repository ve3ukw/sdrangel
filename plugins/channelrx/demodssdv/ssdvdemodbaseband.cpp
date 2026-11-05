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

#include "dsp/dspcommands.h"

#include "ssdvdemodbaseband.h"

MESSAGE_CLASS_DEFINITION(SSDVDemodBaseband::MsgConfigureSSDVDemodBaseband, Message)

SSDVDemodBaseband::SSDVDemodBaseband() :
    m_channelizer(&m_sink)
{
    m_sampleFifo.setSize(SampleSinkFifo::getSizePolicy(48000));

    qDebug("SSDVDemodBaseband::SSDVDemodBaseband");
    QObject::connect(
        &m_sampleFifo,
        &SampleSinkFifo::dataReady,
        this,
        &SSDVDemodBaseband::handleData,
        Qt::QueuedConnection
    );

    QObject::connect(
        &m_inputMessageQueue,
        &MessageQueue::messageEnqueued,
        this,
        &SSDVDemodBaseband::handleInputMessages,
        Qt::QueuedConnection
    );
}

SSDVDemodBaseband::~SSDVDemodBaseband()
{
}

void SSDVDemodBaseband::reset()
{
    QMutexLocker mutexLocker(&m_mutex);
    m_sampleFifo.reset();
}

void SSDVDemodBaseband::setChannel(ChannelAPI *channel)
{
    m_sink.setChannel(channel);
}

void SSDVDemodBaseband::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end)
{
    m_sampleFifo.write(begin, end);
}

void SSDVDemodBaseband::handleData()
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

void SSDVDemodBaseband::handleInputMessages()
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

bool SSDVDemodBaseband::handleMessage(const Message& cmd)
{
    if (MsgConfigureSSDVDemodBaseband::match(cmd))
    {
        QMutexLocker mutexLocker(&m_mutex);
        MsgConfigureSSDVDemodBaseband& cfg = (MsgConfigureSSDVDemodBaseband&) cmd;
        qDebug() << "SSDVDemodBaseband::handleMessage: MsgConfigureSSDVDemodBaseband";

        applySettings(cfg.getSettingsKeys(), cfg.getSettings(), cfg.getForce());

        return true;
    }
    else if (DSPSignalNotification::match(cmd))
    {
        QMutexLocker mutexLocker(&m_mutex);
        DSPSignalNotification& notif = (DSPSignalNotification&) cmd;
        qDebug() << "SSDVDemodBaseband::handleMessage: DSPSignalNotification: basebandSampleRate: " << notif.getSampleRate();
        m_sampleFifo.setSize(SampleSinkFifo::getSizePolicy(notif.getSampleRate()));
        m_channelizer.setBasebandSampleRate(notif.getSampleRate());
        m_sink.applyChannelSettings(m_channelizer.getChannelSampleRate(), m_channelizer.getChannelFrequencyOffset());

        return true;
    }
    else
    {
        return false;
    }
}

void SSDVDemodBaseband::applySettings(const QStringList& settingsKeys, const SSDVDemodSettings& settings, const bool force)
{
    if ((settingsKeys.contains("inputFrequencyOffset") && (settings.m_inputFrequencyOffset != m_settings.m_inputFrequencyOffset))
        || (settingsKeys.contains("baudRate") && (settings.m_baudRate != m_settings.m_baudRate))
        || force)
    {
        m_channelizer.setChannelization(settings.m_baudRate * SSDVDemodSettings::m_samplesPerSymbol, settings.m_inputFrequencyOffset);
        m_sink.applyChannelSettings(m_channelizer.getChannelSampleRate(), m_channelizer.getChannelFrequencyOffset());
    }

    m_sink.applySettings(settingsKeys, settings, force);

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }
}

int SSDVDemodBaseband::getChannelSampleRate() const
{
    return m_channelizer.getChannelSampleRate();
}

void SSDVDemodBaseband::setBasebandSampleRate(int sampleRate)
{
    m_channelizer.setBasebandSampleRate(sampleRate);
    m_sink.applyChannelSettings(m_channelizer.getChannelSampleRate(), m_channelizer.getChannelFrequencyOffset());
}
