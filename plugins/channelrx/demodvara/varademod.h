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

#ifndef INCLUDE_VARADEMOD_H
#define INCLUDE_VARADEMOD_H

#include <memory>

#include <QNetworkRequest>

#include "dsp/basebandsamplesink.h"
#include "channel/channelapi.h"
#include "util/message.h"

#include "varaengine.h"
#include "varademodbaseband.h"
#include "varademodsettings.h"

class QNetworkAccessManager;
class QNetworkReply;
class QThread;
class DeviceAPI;
class ObjectPipe;

// Receive side of a VARA modem. It owns the modem (whose VARA TCP ports are open while the channel exists,
// whether or not the device is running) and drives its clock with the received samples. A VARA Mod on the
// same command port transmits what the modem sends.
class VARADemod : public BasebandSampleSink, public ChannelAPI
{
public:
    class MsgConfigureVARADemod : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const VARADemodSettings& getSettings() const { return m_settings; }
        const QStringList& getSettingsKeys() const { return m_settingsKeys; }
        bool getForce() const { return m_force; }

        static MsgConfigureVARADemod* create(const QStringList& settingsKeys, const VARADemodSettings& settings, bool force)
        {
            return new MsgConfigureVARADemod(settingsKeys, settings, force);
        }

    private:
        VARADemodSettings m_settings;
        QStringList m_settingsKeys;
        bool m_force;

        MsgConfigureVARADemod(const QStringList& settingsKeys, const VARADemodSettings& settings, bool force) :
            Message(),
            m_settings(settings),
            m_settingsKeys(settingsKeys),
            m_force(force)
        {}
    };

    VARADemod(DeviceAPI *deviceAPI);
    virtual ~VARADemod();
    virtual void destroy() { delete this; }
    virtual void setDeviceAPI(DeviceAPI *deviceAPI);
    virtual DeviceAPI *getDeviceAPI() { return m_deviceAPI; }

    using BasebandSampleSink::feed;
    virtual void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, bool po);
    virtual void start();
    virtual void stop();
    virtual void pushMessage(Message *msg) { m_inputMessageQueue.push(msg); }
    virtual QString getSinkName() { return objectName(); }

    virtual void getIdentifier(QString& id) { id = objectName(); }
    virtual QString getIdentifier() const { return objectName(); }
    virtual void getTitle(QString& title) { title = m_settings.m_title; }
    virtual qint64 getCenterFrequency() const { return m_settings.m_inputFrequencyOffset; }
    virtual void setCenterFrequency(qint64 frequency);
    int getChannelSampleRate() const override;

    virtual QByteArray serialize() const;
    virtual bool deserialize(const QByteArray& data);

    virtual int getNbSinkStreams() const { return 1; }
    virtual int getNbSourceStreams() const { return 0; }
    virtual int getStreamIndex() const { return m_settings.m_streamIndex; }

    virtual qint64 getStreamCenterFrequency(int streamIndex, bool sinkElseSource) const
    {
        (void) streamIndex;
        (void) sinkElseSource;
        return m_settings.m_inputFrequencyOffset;
    }

    virtual int webapiSettingsGet(
            SWGSDRangel::SWGChannelSettings& response,
            QString& errorMessage);

    virtual int webapiWorkspaceGet(
            SWGSDRangel::SWGWorkspaceInfo& response,
            QString& errorMessage);

    virtual int webapiSettingsPutPatch(
            bool force,
            const QStringList& channelSettingsKeys,
            SWGSDRangel::SWGChannelSettings& response,
            QString& errorMessage);

    virtual int webapiReportGet(
            SWGSDRangel::SWGChannelReport& response,
            QString& errorMessage);

    static void webapiFormatChannelSettings(
        SWGSDRangel::SWGChannelSettings& response,
        const VARADemodSettings& settings);

    static void webapiUpdateChannelSettings(
            VARADemodSettings& settings,
            const QStringList& channelSettingsKeys,
            SWGSDRangel::SWGChannelSettings& response);

    void getMagSqLevels(double& avg, double& peak, int& nbSamples)
    {
        if (m_running) {
            m_basebandSink->getMagSqLevels(avg, peak, nbSamples);
        } else {
            avg = 0.0; peak = 0.0; nbSamples = 1;
        }
    }

    void getModemLevels(float& rms, float& peak)
    {
        if (m_running) {
            m_basebandSink->getModemLevels(rms, peak);
        } else {
            rms = 0.0f; peak = 0.0f;
        }
    }

    VARAEngine::Status getEngineStatus() const { return m_engine->getStatus(); }
    int getAudioSampleRate() const override { return m_running ? m_basebandSink->getAudioSampleRate() : 0; }
    uint32_t getNumberOfDeviceStreams() const;

    static const char* const m_channelIdURI;
    static const char* const m_channelId;

private:
    DeviceAPI* m_deviceAPI;
    QThread *m_thread;
    VARADemodBaseband* m_basebandSink;
    bool m_running;
    VARADemodSettings m_settings;
    int m_basebandSampleRate; //!< stored from device message used when starting baseband sink
    qint64 m_centerFrequency;
    std::unique_ptr<VARAEngine> m_engine;

    QNetworkAccessManager *m_networkManager;
    QNetworkRequest m_networkRequest;

    virtual bool handleMessage(const Message& cmd);
    void applySettings(const QStringList& settingsKeys, const VARADemodSettings& settings, bool force = false);
    static VARAEngine::Config getEngineConfig(const VARADemodSettings& settings);
    void sendSampleRateToDemodAnalyzer();
    void webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response);
    void webapiReverseSendSettings(const QList<QString>& channelSettingsKeys, const VARADemodSettings& settings, bool force);
    void sendChannelSettings(
        const QList<ObjectPipe*>& pipes,
        const QList<QString>& channelSettingsKeys,
        const VARADemodSettings& settings,
        bool force
    );
    void webapiFormatChannelSettings(
        const QList<QString>& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings *swgChannelSettings,
        const VARADemodSettings& settings,
        bool force
    );

private slots:
    void networkManagerFinished(QNetworkReply *reply);
    void handleIndexInDeviceSetChanged(int index);
};

#endif // INCLUDE_VARADEMOD_H
