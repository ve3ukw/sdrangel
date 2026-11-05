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

#ifndef INCLUDE_SSDVMOD_H
#define INCLUDE_SSDVMOD_H

#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>

#include "dsp/basebandsamplesource.h"
#include "channel/channelapi.h"
#include "util/message.h"

#include "ssdvmodframer.h"
#include "ssdvmodsettings.h"

class QNetworkAccessManager;
class QNetworkReply;
class QThread;
class DeviceAPI;
class SSDVModBaseband;
class ObjectPipe;

class SSDVMod : public BasebandSampleSource, public ChannelAPI {
public:
    class MsgConfigureSSDVMod : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const SSDVModSettings& getSettings() const { return m_settings; }
        const QStringList& getSettingsKeys() const { return m_settingsKeys; }
        bool getForce() const { return m_force; }

        static MsgConfigureSSDVMod* create(const QStringList& settingsKeys, const SSDVModSettings& settings, bool force)
        {
            return new MsgConfigureSSDVMod(settingsKeys, settings, force);
        }

    private:
        SSDVModSettings m_settings;
        QStringList m_settingsKeys;
        bool m_force;

        MsgConfigureSSDVMod(const QStringList& settingsKeys, const SSDVModSettings& settings, bool force) :
            Message(),
            m_settings(settings),
            m_settingsKeys(settingsKeys),
            m_force(force)
        { }
    };

    //! Send the image (or the next image of the directory), then more if repeat is on
    class MsgTx : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        static MsgTx* create() {
            return new MsgTx();
        }

    private:
        MsgTx() :
            Message()
        { }
    };

    //! Stop sending, and don't repeat
    class MsgStop : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        static MsgStop* create() {
            return new MsgStop();
        }

    private:
        MsgStop() :
            Message()
        { }
    };

    SSDVMod(DeviceAPI *deviceAPI);
    virtual ~SSDVMod();
    virtual void destroy() { delete this; }
    virtual void setDeviceAPI(DeviceAPI *deviceAPI);
    virtual DeviceAPI *getDeviceAPI() { return m_deviceAPI; }

    virtual void start();
    virtual void stop();
    virtual void pull(SampleVector::iterator& begin, unsigned int nbSamples);
    virtual void pushMessage(Message *msg) { m_inputMessageQueue.push(msg); }
    virtual QString getSourceName() { return objectName(); }

    virtual void getIdentifier(QString& id) { id = objectName(); }
    virtual QString getIdentifier() const { return objectName(); }
    virtual void getTitle(QString& title) { title = m_settings.m_title; }
    virtual qint64 getCenterFrequency() const { return m_settings.m_inputFrequencyOffset; }
    int getChannelSampleRate() const override;
    virtual void setCenterFrequency(qint64 frequency);

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

    virtual int webapiActionsPost(
            const QStringList& channelActionsKeys,
            SWGSDRangel::SWGChannelActions& query,
            QString& errorMessage);

    static void webapiFormatChannelSettings(
        SWGSDRangel::SWGChannelSettings& response,
        const SSDVModSettings& settings);

    static void webapiUpdateChannelSettings(
            SSDVModSettings& settings,
            const QStringList& channelSettingsKeys,
            SWGSDRangel::SWGChannelSettings& response);

    double getMagSq() const;
    void setLevelMeter(QObject *levelMeter);
    uint32_t getNumberOfDeviceStreams() const;
    bool isTransmitting() const { return m_transmitting; }
    bool isRepeating() const { return m_repeatTimer.isActive(); }

    // Image files that would be sent, in order
    static QStringList getImageFiles(const QString& filenameOrDirectory);

    static const char* const m_channelIdURI;
    static const char* const m_channelId;

private:
    DeviceAPI* m_deviceAPI;
    QThread *m_thread;
    SSDVModBaseband* m_basebandSource;
    SSDVModSettings m_settings;
    SSDVModFramer m_framer;

    bool m_transmitting;
    bool m_stopped;          //!< Stop requested, so don't repeat
    int m_txImageId;
    int m_packetsSent;
    int m_packetCount;
    int m_imagesSent;
    int m_fileIndex;         //!< Next image of a directory
    QString m_error;
    QTimer m_repeatTimer;

    QNetworkAccessManager *m_networkManager;
    QNetworkRequest m_networkRequest;

    virtual bool handleMessage(const Message& cmd);
    void applySettings(const QStringList& settingsKeys, const SSDVModSettings& settings, bool force = false);
    bool transmitNextImage();
    void setError(const QString& error);
    void sendSampleRateToDemodAnalyzer();
    void webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response);
    void webapiReverseSendSettings(const QList<QString>& channelSettingsKeys, const SSDVModSettings& settings, bool force);
    void sendChannelSettings(
        const QList<ObjectPipe*>& pipes,
        const QList<QString>& channelSettingsKeys,
        const SSDVModSettings& settings,
        bool force
    );
    void webapiFormatChannelSettings(
        const QList<QString>& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings *swgChannelSettings,
        const SSDVModSettings& settings,
        bool force
    );

private slots:
    void networkManagerFinished(QNetworkReply *reply);
    void repeat();
};

#endif // INCLUDE_SSDVMOD_H
