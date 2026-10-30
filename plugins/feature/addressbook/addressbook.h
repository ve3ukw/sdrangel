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

#ifndef INCLUDE_FEATURE_ADDRESSBOOK_H_
#define INCLUDE_FEATURE_ADDRESSBOOK_H_

#include <QStringList>

#include "feature/feature.h"
#include "util/message.h"

#include "addressbooksettings.h"

class WebAPIAdapterInterface;
class ChannelAPI;

class AddressBook : public Feature
{
    Q_OBJECT
public:
    class MsgConfigureAddressBook : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const AddressBookSettings& getSettings() const { return m_settings; }
        const QStringList& getSettingsKeys() const { return m_settingsKeys; }
        bool getForce() const { return m_force; }

        static MsgConfigureAddressBook* create(const AddressBookSettings& settings, const QStringList& settingsKeys, bool force) {
            return new MsgConfigureAddressBook(settings, settingsKeys, force);
        }

    private:
        AddressBookSettings m_settings;
        QStringList m_settingsKeys;
        bool m_force;

        MsgConfigureAddressBook(const AddressBookSettings& settings, const QStringList& settingsKeys, bool force) :
            Message(),
            m_settings(settings),
            m_settingsKeys(settingsKeys),
            m_force(force)
        { }
    };

    /// Tune a channel to an address book entry
    class MsgTune : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const AddressBookEntry& getEntry() const { return m_entry; }
        const QString& getChannelId() const { return m_channelId; }

        static MsgTune* create(const AddressBookEntry& entry, const QString& channelId) {
            return new MsgTune(entry, channelId);
        }

    private:
        AddressBookEntry m_entry;
        QString m_channelId;

        MsgTune(const AddressBookEntry& entry, const QString& channelId) :
            Message(),
            m_entry(entry),
            m_channelId(channelId)
        { }
    };

    /// Channel shown or hidden when channels are switched for an entry's channel type
    struct ChannelSwitch {
        int m_deviceSetIndex;
        int m_channelIndex;
        bool m_active;          //!< Shown and unmuted if true, else hidden and muted
    };

    /// Result of a MsgTune, sent to the GUI
    class MsgTuneResult : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        bool getSuccess() const { return m_success; }
        const QString& getMessage() const { return m_message; }
        const QList<ChannelSwitch>& getChannelSwitches() const { return m_channelSwitches; }
        int getTunedDeviceSetIndex() const { return m_tunedDeviceSetIndex; }
        int getTunedChannelIndex() const { return m_tunedChannelIndex; }

        static MsgTuneResult* create(bool success, const QString& message, const QList<ChannelSwitch>& channelSwitches,
            int tunedDeviceSetIndex, int tunedChannelIndex) {
            return new MsgTuneResult(success, message, channelSwitches, tunedDeviceSetIndex, tunedChannelIndex);
        }

    private:
        bool m_success;
        QString m_message;
        QList<ChannelSwitch> m_channelSwitches;
        int m_tunedDeviceSetIndex; //!< Channel that was tuned, which may differ from the selected channel. -1 if none
        int m_tunedChannelIndex;

        MsgTuneResult(bool success, const QString& message, const QList<ChannelSwitch>& channelSwitches,
                int tunedDeviceSetIndex, int tunedChannelIndex) :
            Message(),
            m_success(success),
            m_message(message),
            m_channelSwitches(channelSwitches),
            m_tunedDeviceSetIndex(tunedDeviceSetIndex),
            m_tunedChannelIndex(tunedChannelIndex)
        { }
    };

    AddressBook(WebAPIAdapterInterface *webAPIAdapterInterface);
    ~AddressBook() override = default;

    void destroy() override { delete this; }
    bool handleMessage(const Message& cmd) override;

    void getIdentifier(QString& id) const override { id = objectName(); }
    QString getIdentifier() const override { return objectName(); }
    void getTitle(QString& title) const override { title = m_settings.m_title; }

    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data) override;

    /// Tune the channel with the given ID (e.g. "R0:1") to the entry. Returns false and sets errorMessage on failure.
    /// If autoSwitchChannels is set, channelSwitches is set to the channels to show and hide
    static bool tune(const AddressBookEntry& entry, const QString& channelId, bool autoSwitchChannels,
        QList<ChannelSwitch>& channelSwitches, ChannelAPI*& tunedChannel, QString& errorMessage);
    /// Get center frequency and sample rate of the Rx device, or Rx stream of a MIMO device, that a receive channel is attached to
    static bool getRxCenterFrequency(unsigned int deviceSetIndex, const ChannelAPI *channel, qint64& centerFrequency, int& sampleRate);
    /// Set center frequency of the Rx device, or Rx stream of a MIMO device, that a receive channel is attached to
    static bool setRxCenterFrequency(unsigned int deviceSetIndex, const ChannelAPI *channel, qint64 centerFrequency);

    static const char* const m_featureIdURI;
    static const char* const m_featureId;

private:
    static ChannelAPI *getLinkedRxChannel(const AddressBookEntry& entry, const ChannelAPI *channel);
    static ChannelAPI *switchChannels(const AddressBookEntry& entry, const ChannelAPI *channel, QList<ChannelSwitch>& channelSwitches);
    static bool setRotatorAzimuth(float azimuth);
    static int getCTCSSIndex(float ctcss);
    static void tuneLinkedChannels(const AddressBookEntry& entry, const ChannelAPI *channel, qint64 rxOffset, int ctcssIndex);
    AddressBookSettings m_settings;

    void applySettings(const AddressBookSettings& settings, const QStringList& settingsKeys, bool force = false);
};

#endif // INCLUDE_FEATURE_ADDRESSBOOK_H_
