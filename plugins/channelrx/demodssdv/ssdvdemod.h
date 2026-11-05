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

#ifndef INCLUDE_SSDVDEMOD_H
#define INCLUDE_SSDVDEMOD_H

#include <QFile>
#include <QList>
#include <QNetworkRequest>

#include "dsp/basebandsamplesink.h"
#include "dsp/scopevis.h"
#include "channel/channelapi.h"
#include "util/message.h"
#include "util/ssdvcodec.h"

#include "ssdvdemodbaseband.h"
#include "ssdvdemodframeparser.h"
#include "ssdvdemodsettings.h"

class QNetworkAccessManager;
class QNetworkReply;
class QThread;
class DeviceAPI;
class ObjectPipe;

class SSDVDemod : public BasebandSampleSink, public ChannelAPI
{
public:
    class MsgConfigureSSDVDemod : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const SSDVDemodSettings& getSettings() const { return m_settings; }
        const QStringList& getSettingsKeys() const { return m_settingsKeys; }
        bool getForce() const { return m_force; }

        static MsgConfigureSSDVDemod* create(const QStringList& settingsKeys, const SSDVDemodSettings& settings, bool force)
        {
            return new MsgConfigureSSDVDemod(settingsKeys, settings, force);
        }

    private:
        SSDVDemodSettings m_settings;
        QStringList m_settingsKeys;
        bool m_force;

        MsgConfigureSSDVDemod(const QStringList& settingsKeys, const SSDVDemodSettings& settings, bool force) :
            Message(),
            m_settings(settings),
            m_settingsKeys(settingsKeys),
            m_force(force)
        {}
    };

    //! Forget the images being received (saving them first if auto save is on)
    class MsgClear : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        static MsgClear* create() {
            return new MsgClear();
        }

    private:
        MsgClear() :
            Message()
        {}
    };

    SSDVDemod(DeviceAPI *deviceAPI);
    virtual ~SSDVDemod();
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
        const SSDVDemodSettings& settings);

    static void webapiUpdateChannelSettings(
            SSDVDemodSettings& settings,
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

    uint32_t getNumberOfDeviceStreams() const;
    ScopeVis *getScopeSink() { return &m_scopeVis; }
    bool isLocked() const { return m_running && m_basebandSink->isLocked(); }
    float getFrequencyOffset() const { return m_running ? m_basebandSink->getFrequencyOffset() : 0.0f; }
    float getEvm() const { return m_running ? m_basebandSink->getEvm() : 0.0f; }
    int getFramesReceived() const { return m_framesReceived; }
    int getFramesBad() const { return m_framesBad + (m_running ? m_basebandSink->getFramesBad() : 0); }
    int getPacketsReceived() const { return m_packetsReceived; }
    int getImagesReceived() const { return m_imagesReceived; }
    // Saves the most recent image (as received so far) to fileName
    bool saveImage(const QString& fileName);

    static const char* const m_channelIdURI;
    static const char* const m_channelId;

private:
    struct Image
    {
        SSDV::ImageDecoder *m_decoder;
        bool m_saved;
    };

    static const int m_maxImages = 4; //!< Images being received at once, e.g. with interleaved repeats

    DeviceAPI* m_deviceAPI;
    QThread *m_thread;
    SSDVDemodBaseband* m_basebandSink;
    bool m_running;
    SSDVDemodSettings m_settings;
    int m_basebandSampleRate; //!< stored from device message used when starting baseband sink
    qint64 m_centerFrequency;

    QList<Image> m_images; //!< Most recent first
    SSDVDemodFrameParser m_frameParser;
    int m_framesReceived;
    int m_framesBad;       //!< Of baseband sinks that were stopped
    int m_packetsReceived;
    int m_imagesReceived;
    QString m_lastSavedFile;
    QFile m_framesFile;
    ScopeVis m_scopeVis;

    QNetworkAccessManager *m_networkManager;
    QNetworkRequest m_networkRequest;

    virtual bool handleMessage(const Message& cmd);
    void applySettings(const QStringList& settingsKeys, const SSDVDemodSettings& settings, bool force = false);
    void handleFrame(const QByteArray& data, const QDateTime& dateTime);
    void handlePacket(const QByteArray& data, SSDV::Variant variant, const QDateTime& dateTime);
    void saveFrame(const QByteArray& data);
    QString autoSaveImage(Image& image, bool complete);
    void clearImages();
    void sendSampleRateToDemodAnalyzer();
    void webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response);
    void webapiReverseSendSettings(const QList<QString>& channelSettingsKeys, const SSDVDemodSettings& settings, bool force);
    void sendChannelSettings(
        const QList<ObjectPipe*>& pipes,
        const QList<QString>& channelSettingsKeys,
        const SSDVDemodSettings& settings,
        bool force
    );
    void webapiFormatChannelSettings(
        const QList<QString>& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings *swgChannelSettings,
        const SSDVDemodSettings& settings,
        bool force
    );

private slots:
    void networkManagerFinished(QNetworkReply *reply);
    void handleIndexInDeviceSetChanged(int index);
};

#endif // INCLUDE_SSDVDEMOD_H
