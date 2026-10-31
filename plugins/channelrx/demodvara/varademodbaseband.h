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

#ifndef INCLUDE_VARADEMODBASEBAND_H
#define INCLUDE_VARADEMODBASEBAND_H

#include <QObject>
#include <QRecursiveMutex>

#include "dsp/samplesinkfifo.h"
#include "dsp/downchannelizer.h"
#include "util/message.h"
#include "util/messagequeue.h"

#include "varademodsink.h"

class ChannelAPI;
class VARAEngine;

class VARADemodBaseband : public QObject
{
    Q_OBJECT
public:
    class MsgConfigureVARADemodBaseband : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const VARADemodSettings& getSettings() const { return m_settings; }
        const QStringList& getSettingsKeys() const { return m_settingsKeys; }
        bool getForce() const { return m_force; }

        static MsgConfigureVARADemodBaseband* create(const QStringList& settingsKeys, const VARADemodSettings& settings, bool force)
        {
            return new MsgConfigureVARADemodBaseband(settingsKeys, settings, force);
        }

    private:
        VARADemodSettings m_settings;
        QStringList m_settingsKeys;
        bool m_force;

        MsgConfigureVARADemodBaseband(const QStringList& settingsKeys, const VARADemodSettings& settings, bool force) :
            Message(),
            m_settings(settings),
            m_settingsKeys(settingsKeys),
            m_force(force)
        { }
    };

    VARADemodBaseband();
    ~VARADemodBaseband();
    void reset();
    void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end);
    MessageQueue *getInputMessageQueue() { return &m_inputMessageQueue; } //!< Get the queue for asynchronous inbound communication
    int getChannelSampleRate() const;
    void getMagSqLevels(double& avg, double& peak, int& nbSamples) { m_sink.getMagSqLevels(avg, peak, nbSamples); }
    void getModemLevels(float& rms, float& peak) { m_sink.getModemLevels(rms, peak); }
    void setEngine(VARAEngine *engine) { m_sink.setEngine(engine); }
    int getAudioSampleRate() const { return m_sink.getAudioSampleRate(); }
    void setBasebandSampleRate(int sampleRate);
    void setChannel(ChannelAPI *channel);
    void setFifoLabel(const QString& label) { m_sampleFifo.setLabel(label); }
    void setAudioFifoLabel(const QString& label) { m_sink.setAudioFifoLabel(label); }

private:
    SampleSinkFifo m_sampleFifo;
    DownChannelizer m_channelizer;
    int m_channelSampleRate;
    VARADemodSink m_sink;
    MessageQueue m_inputMessageQueue; //!< Queue for asynchronous inbound communication
    VARADemodSettings m_settings;
    QRecursiveMutex m_mutex;

    bool handleMessage(const Message& cmd);
    void applySettings(const QStringList& settingsKeys, const VARADemodSettings& settings, bool force = false);

private slots:
    void handleInputMessages();
    void handleData(); //!< Handle data when samples have to be processed
};

#endif // INCLUDE_VARADEMODBASEBAND_H
