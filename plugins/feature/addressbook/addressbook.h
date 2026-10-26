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

    /// Result of a MsgTune, sent to the GUI
    class MsgTuneResult : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        bool getSuccess() const { return m_success; }
        const QString& getMessage() const { return m_message; }

        static MsgTuneResult* create(bool success, const QString& message) {
            return new MsgTuneResult(success, message);
        }

    private:
        bool m_success;
        QString m_message;

        MsgTuneResult(bool success, const QString& message) :
            Message(),
            m_success(success),
            m_message(message)
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

    /// Tune the channel with the given ID (e.g. "R0:1") to the entry. Returns false and sets errorMessage on failure
    static bool tune(const AddressBookEntry& entry, const QString& channelId, QString& errorMessage);

    static const char* const m_featureIdURI;
    static const char* const m_featureId;

private:
    AddressBookSettings m_settings;

    void applySettings(const AddressBookSettings& settings, const QStringList& settingsKeys, bool force = false);
};

#endif // INCLUDE_FEATURE_ADDRESSBOOK_H_
