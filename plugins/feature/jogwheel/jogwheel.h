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

#ifndef INCLUDE_FEATURE_JOGWHEEL_H_
#define INCLUDE_FEATURE_JOGWHEEL_H_

#include <QElapsedTimer>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>

#include "feature/feature.h"
#include "util/message.h"

#include "jogwheelsettings.h"

class WebAPIAdapterInterface;
class QNetworkAccessManager;
class QNetworkReply;
class QUdpSocket;

namespace SWGSDRangel {
    class SWGFeatureSettings;
    class SWGFeatureReport;
    class SWGFeatureActions;
}

// Tunes a channel (or a device's centre frequency) by relative steps, like the tuning knob of an HF rig.
// Steps come from the GUI's knob, from hardware over UDP or from the Web API, and are applied as deltas,
// so that they combine with Doppler correction from the Satellite Tracker, which also moves the offset.
// An optional linked target follows the main one, in the opposite direction for inverting transponders.
class JogWheel : public Feature
{
    Q_OBJECT
public:
    class MsgConfigureJogWheel : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const JogWheelSettings& getSettings() const { return m_settings; }
        const QStringList& getSettingsKeys() const { return m_settingsKeys; }
        bool getForce() const { return m_force; }

        static MsgConfigureJogWheel* create(const JogWheelSettings& settings, const QStringList& settingsKeys, bool force) {
            return new MsgConfigureJogWheel(settings, settingsKeys, force);
        }

    private:
        JogWheelSettings m_settings;
        QStringList m_settingsKeys;
        bool m_force;

        MsgConfigureJogWheel(const JogWheelSettings& settings, const QStringList& settingsKeys, bool force) :
            Message(),
            m_settings(settings),
            m_settingsKeys(settingsKeys),
            m_force(force)
        { }
    };

    // Turn the wheel by a number of detents (step size and acceleration apply), or move by a number of Hz
    class MsgJog : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        int getTicks() const { return m_ticks; }
        qint64 getDeltaHz() const { return m_deltaHz; }

        static MsgJog* create(int ticks) { return new MsgJog(ticks, 0); }
        static MsgJog* createHz(qint64 deltaHz) { return new MsgJog(0, deltaHz); }

    private:
        int m_ticks;
        qint64 m_deltaHz;

        MsgJog(int ticks, qint64 deltaHz) :
            Message(),
            m_ticks(ticks),
            m_deltaHz(deltaHz)
        { }
    };

    class MsgResetTotal : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        static MsgResetTotal* create() { return new MsgResetTotal(); }

    private:
        MsgResetTotal() : Message() { }
    };

    // To GUI: total change since reset and the multiplier acceleration applied to the last step
    class MsgReportTotal : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        qint64 getTotalHz() const { return m_totalHz; }
        int getMultiplier() const { return m_multiplier; }

        static MsgReportTotal* create(qint64 totalHz, int multiplier) { return new MsgReportTotal(totalHz, multiplier); }

    private:
        qint64 m_totalHz;
        int m_multiplier;

        MsgReportTotal(qint64 totalHz, int multiplier) :
            Message(),
            m_totalHz(totalHz),
            m_multiplier(multiplier)
        { }
    };

    // To GUI: UDP listener state. Empty error when listening (or disabled)
    class MsgReportUDP : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        bool getListening() const { return m_listening; }
        const QString& getError() const { return m_error; }

        static MsgReportUDP* create(bool listening, const QString& error) { return new MsgReportUDP(listening, error); }

    private:
        bool m_listening;
        QString m_error;

        MsgReportUDP(bool listening, const QString& error) :
            Message(),
            m_listening(listening),
            m_error(error)
        { }
    };

    JogWheel(WebAPIAdapterInterface *webAPIAdapterInterface);
    ~JogWheel() override;
    void destroy() override { delete this; }
    bool handleMessage(const Message& cmd) override;

    void getIdentifier(QString& id) const override { id = objectName(); }
    QString getIdentifier() const override { return objectName(); }
    void getTitle(QString& title) const override { title = m_settings.m_title; }

    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data) override;

    int webapiSettingsGet(
        SWGSDRangel::SWGFeatureSettings& response,
        QString& errorMessage) override;

    int webapiSettingsPutPatch(
        bool force,
        const QStringList& featureSettingsKeys,
        SWGSDRangel::SWGFeatureSettings& response,
        QString& errorMessage) override;

    int webapiReportGet(
        SWGSDRangel::SWGFeatureReport& response,
        QString& errorMessage) override;

    int webapiActionsPost(
        const QStringList& featureActionsKeys,
        SWGSDRangel::SWGFeatureActions& query,
        QString& errorMessage) override;

    static void webapiFormatFeatureSettings(
        SWGSDRangel::SWGFeatureSettings& response,
        const JogWheelSettings& settings);

    static void webapiUpdateFeatureSettings(
        JogWheelSettings& settings,
        const QStringList& featureSettingsKeys,
        SWGSDRangel::SWGFeatureSettings& response);

    // Frequency of a target: channel centre frequency, or device centre frequency. False if not available
    static bool getTargetFrequency(const QString& target, qint64& frequency);

    static const char* const m_featureIdURI;
    static const char* const m_featureId;

private:
    JogWheelSettings m_settings;
    QNetworkAccessManager *m_networkManager;
    QNetworkRequest m_networkRequest;
    QUdpSocket *m_udpSocket;
    QTimer m_flushTimer;
    QElapsedTimer m_clock;
    qint64 m_pendingHz;     //!< Change not yet applied to the targets
    qint64 m_totalHz;       //!< Change applied since the last reset
    qint64 m_lastTickMs;
    double m_tickRate;      //!< Smoothed detents per second, for acceleration
    int m_lastDirection;
    int m_multiplier;

    // Changes are gathered and applied at this interval, as a channel only takes a new offset once it has
    // processed the settings message, so read-modify-write of each step in turn would lose steps
    static const int m_flushIntervalMs = 25;

    void applySettings(const JogWheelSettings& settings, const QStringList& settingsKeys, bool force = false);
    void webapiReverseSendSettings(const QStringList& featureSettingsKeys, const JogWheelSettings& settings, bool force);
    void openUDP();
    void closeUDP();
    void jog(int ticks);
    void moveHz(qint64 deltaHz);
    void resetTotal();
    void reportTotal();
    void processCommand(const QString& command);
    void cycleStep();
    static bool moveTarget(const QString& target, qint64 deltaHz);
    static bool getChannelCenterFrequency(unsigned int deviceSetIndex, unsigned int channelIndex, double& centerFrequency);

private slots:
    void networkManagerFinished(QNetworkReply *reply);
    void flush();
    void readUDP();
};

#endif // INCLUDE_FEATURE_JOGWHEEL_H_
