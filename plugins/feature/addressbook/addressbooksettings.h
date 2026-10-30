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

#ifndef INCLUDE_FEATURE_ADDRESSBOOKSETTINGS_H_
#define INCLUDE_FEATURE_ADDRESSBOOKSETTINGS_H_

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include "addressbookentry.h"

class Serializable;

struct AddressBookSettings
{
    QList<AddressBookEntry> m_entries;
    QString m_selectedChannel;      //!< Channel to tune, e.g. "R0:1 NFMDemod"
    QString m_groupFilter;          //!< Only show entries in this group. Empty for all
    bool m_autoSwitchChannels;      //!< Show and unmute the channels for the entry's channel type, hide and mute the others
    QString m_title;
    quint32 m_rgbColor;
    bool m_useReverseAPI;
    QString m_reverseAPIAddress;
    uint16_t m_reverseAPIPort;
    uint16_t m_reverseAPIFeatureSetIndex;
    uint16_t m_reverseAPIFeatureIndex;
    Serializable *m_rollupState;
    int m_workspaceIndex;
    QByteArray m_geometryBytes;

    AddressBookSettings();
    ~AddressBookSettings() = default;

    void resetToDefaults();
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
    void setRollupState(Serializable *rollupState) { m_rollupState = rollupState; }
    void applySettings(const QStringList& settingsKeys, const AddressBookSettings& settings);

    static QByteArray serializeEntries(const QList<AddressBookEntry>& entries);
    static bool deserializeEntries(const QByteArray& data, QList<AddressBookEntry>& entries);
};

#endif // INCLUDE_FEATURE_ADDRESSBOOKSETTINGS_H_
