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

#ifndef INCLUDE_VARAMOD_H
#define INCLUDE_VARAMOD_H

#include <atomic>
#include <memory>

#include <QNetworkRequest>

#include "dsp/basebandsamplesource.h"
#include "channel/channelapi.h"
#include "util/message.h"

#include "varamodsettings.h"

class QNetworkAccessManager;
class QNetworkReply;
class QThread;
class DeviceAPI;
class ObjectPipe;
class UDPPTT;
class VARATxLink;
class VARAModBaseband;

// Transmit side of a VARA modem: transmits the modem of the VARA Demod on the same command port, and keys a
// UDP PTT sequencer (E.g. pttseq on a PlutoSky) as the modem keys.
//
// The modem can only key while this channel is running and not muted, so it never keys a transmitter that
// has nothing to send.
class VARAMod : public BasebandSampleSource, public ChannelAPI {
public:
    class MsgConfigureVARAMod : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const VARAModSettings& getSettings() const { return m_settings; }
        const QStringList& getSettingsKeys() const { return m_settingsKeys; }
        bool getForce() const { return m_force; }

        static MsgConfigureVARAMod* create(const QStringList& settingsKeys, const VARAModSettings& settings, bool force)
        {
            return new MsgConfigureVARAMod(settingsKeys, settings, force);
        }

    private:
        VARAModSettings m_settings;
        QStringList m_settingsKeys;
        bool m_force;

        MsgConfigureVARAMod(const QStringList& settingsKeys, const VARAModSettings& settings, bool force) :
            Message(),
            m_settings(settings),
            m_settingsKeys(settingsKeys),
            m_force(force)
        { }
    };

    class MsgQueryPTT : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        static MsgQueryPTT* create() {
            return new MsgQueryPTT();
        }

    private:
        MsgQueryPTT() :
            Message()
        { }
    };

    struct Status
    {
        bool m_linked = false;         //!< Attached to the engine's command port
        bool m_engineAttached = false; //!< A VARA Demod's modem feeds the link
        int m_engineMode = 0;
        bool m_keyed = false;
        int m_fifoMs = 0;
        unsigned int m_underflows = 0;
        QString m_pttReply;
    };

    explicit VARAMod(DeviceAPI *deviceAPI);
    ~VARAMod() final;
    void destroy() final { delete this; }
    void setDeviceAPI(DeviceAPI *deviceAPI) final;
    DeviceAPI *getDeviceAPI() final { return m_deviceAPI; }

    void start() final;
    void stop() final;
    void pull(SampleVector::iterator& begin, unsigned int nbSamples) final;
    void pushMessage(Message *msg) final { m_inputMessageQueue.push(msg); }
    QString getSourceName() final { return objectName(); }

    void getIdentifier(QString& id) final { id = objectName(); }
    QString getIdentifier() const final { return objectName(); }
    void getTitle(QString& title) final { title = m_settings.m_title; }
    qint64 getCenterFrequency() const final { return m_settings.m_inputFrequencyOffset; }
    int getChannelSampleRate() const override;
    void setCenterFrequency(qint64 frequency) final;

    QByteArray serialize() const final;
    bool deserialize(const QByteArray& data) final;

    int getNbSinkStreams() const final { return 1; }
    int getNbSourceStreams() const final { return 0; }
    int getStreamIndex() const final { return m_settings.m_streamIndex; }

    qint64 getStreamCenterFrequency(int streamIndex, bool sinkElseSource) const final
    {
        (void) streamIndex;
        (void) sinkElseSource;
        return m_settings.m_inputFrequencyOffset;
    }

    int webapiSettingsGet(
                SWGSDRangel::SWGChannelSettings& response,
                QString& errorMessage) final;

    int webapiWorkspaceGet(
            SWGSDRangel::SWGWorkspaceInfo& response,
            QString& errorMessage) final;

    int webapiSettingsPutPatch(
                bool force,
                const QStringList& channelSettingsKeys,
                SWGSDRangel::SWGChannelSettings& response,
                QString& errorMessage) final;

    int webapiReportGet(
                SWGSDRangel::SWGChannelReport& response,
                QString& errorMessage) final;

    static void webapiFormatChannelSettings(
        SWGSDRangel::SWGChannelSettings& response,
        const VARAModSettings& settings);

    static void webapiUpdateChannelSettings(
            VARAModSettings& settings,
            const QStringList& channelSettingsKeys,
            SWGSDRangel::SWGChannelSettings& response);

    double getMagSq() const;
    void setLevelMeter(QObject *levelMeter) { m_levelMeter = levelMeter; }
    uint32_t getNumberOfDeviceStreams() const;
    Status getStatus() const;

    static const char* const m_channelIdURI;
    static const char* const m_channelId;
    static const int m_fifoWindowMs = 40; //!< Rx/Tx timing jitter the transmit FIFO takes up between transmissions

private:
    DeviceAPI* m_deviceAPI;
    QThread *m_thread;
    bool m_running;
    VARAModBaseband* m_basebandSource;
    VARAModSettings m_settings;
    int m_basebandSampleRate;
    qint64 m_centerFrequency;
    std::shared_ptr<VARATxLink> m_txLink;
    int m_txLinkPort;
    std::shared_ptr<UDPPTT> m_ptt;                    //!< Shared with the link's PTT callback, which runs on the engine's thread
    std::shared_ptr<std::atomic<bool>> m_pttEnabled;
    QString m_pttReply;

    QNetworkAccessManager *m_networkManager;
    QNetworkRequest m_networkRequest;
    QObject *m_levelMeter;

    bool handleMessage(const Message& cmd) final;
    void applySettings(const QStringList& settingsKeys, const VARAModSettings& settings, bool force = false);
    void updateTxLink();
    void applyTxTiming();
    void webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response) const;
    void webapiReverseSendSettings(const QList<QString>& channelSettingsKeys, const VARAModSettings& settings, bool force);
    void sendChannelSettings(
        const QList<ObjectPipe*>& pipes,
        const QList<QString>& channelSettingsKeys,
        const VARAModSettings& settings,
        bool force
    );
    void webapiFormatChannelSettings(
        const QList<QString>& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings *swgChannelSettings,
        const VARAModSettings& settings,
        bool force
    );

private slots:
    void networkManagerFinished(QNetworkReply *reply) const;
    void pttReplyReceived(const QString& reply);
};

#endif // INCLUDE_VARAMOD_H
