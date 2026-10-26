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

#include <QColor>

#include "util/simpleserializer.h"
#include "settings/serializable.h"

#include "addressbooksettings.h"

AddressBookSettings::AddressBookSettings() :
    m_rollupState(nullptr)
{
    resetToDefaults();
}

void AddressBookSettings::resetToDefaults()
{
    m_entries.clear();
    m_selectedChannel.clear();
    m_groupFilter.clear();
    m_title = "Address Book";
    m_rgbColor = QColor(212, 170, 0).rgb();
    m_useReverseAPI = false;
    m_reverseAPIAddress = "127.0.0.1";
    m_reverseAPIPort = 8888;
    m_reverseAPIFeatureSetIndex = 0;
    m_reverseAPIFeatureIndex = 0;
    m_workspaceIndex = 0;
    m_geometryBytes.clear();
}

QByteArray AddressBookSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeBlob(1, serializeEntries(m_entries));
    s.writeString(2, m_selectedChannel);
    s.writeString(3, m_groupFilter);
    s.writeString(4, m_title);
    s.writeU32(5, m_rgbColor);
    s.writeBool(6, m_useReverseAPI);
    s.writeString(7, m_reverseAPIAddress);
    s.writeU32(8, m_reverseAPIPort);
    s.writeU32(9, m_reverseAPIFeatureSetIndex);
    s.writeU32(10, m_reverseAPIFeatureIndex);

    if (m_rollupState) {
        s.writeBlob(11, m_rollupState->serialize());
    }

    s.writeS32(12, m_workspaceIndex);
    s.writeBlob(13, m_geometryBytes);

    return s.final();
}

bool AddressBookSettings::deserialize(const QByteArray& data)
{
    SimpleDeserializer d(data);

    if (!d.isValid())
    {
        resetToDefaults();
        return false;
    }

    if (d.getVersion() == 1)
    {
        QByteArray bytetmp;
        uint32_t utmp;

        d.readBlob(1, &bytetmp);
        deserializeEntries(bytetmp, m_entries);
        d.readString(2, &m_selectedChannel, "");
        d.readString(3, &m_groupFilter, "");
        d.readString(4, &m_title, "Address Book");
        d.readU32(5, &m_rgbColor, QColor(212, 170, 0).rgb());
        d.readBool(6, &m_useReverseAPI, false);
        d.readString(7, &m_reverseAPIAddress, "127.0.0.1");
        d.readU32(8, &utmp, 0);

        if ((utmp > 1023) && (utmp < 65535)) {
            m_reverseAPIPort = utmp;
        } else {
            m_reverseAPIPort = 8888;
        }

        d.readU32(9, &utmp, 0);
        m_reverseAPIFeatureSetIndex = utmp > 99 ? 99 : utmp;
        d.readU32(10, &utmp, 0);
        m_reverseAPIFeatureIndex = utmp > 99 ? 99 : utmp;

        if (m_rollupState)
        {
            d.readBlob(11, &bytetmp);
            m_rollupState->deserialize(bytetmp);
        }

        d.readS32(12, &m_workspaceIndex, 0);
        d.readBlob(13, &m_geometryBytes);

        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

QByteArray AddressBookSettings::serializeEntries(const QList<AddressBookEntry>& entries)
{
    SimpleSerializer s(1);

    s.writeS32(1, entries.size());

    for (int i = 0; i < entries.size(); i++) {
        s.writeBlob(100 + i, entries[i].serialize());
    }

    return s.final();
}

bool AddressBookSettings::deserializeEntries(const QByteArray& data, QList<AddressBookEntry>& entries)
{
    SimpleDeserializer d(data);

    entries.clear();

    if (!d.isValid() || (d.getVersion() != 1)) {
        return false;
    }

    int count;
    d.readS32(1, &count, 0);

    for (int i = 0; i < count; i++)
    {
        QByteArray blob;
        AddressBookEntry entry;

        d.readBlob(100 + i, &blob);

        if (entry.deserialize(blob)) {
            entries.append(entry);
        }
    }

    return true;
}

void AddressBookSettings::applySettings(const QStringList& settingsKeys, const AddressBookSettings& settings)
{
    if (settingsKeys.contains("entries")) {
        m_entries = settings.m_entries;
    }
    if (settingsKeys.contains("selectedChannel")) {
        m_selectedChannel = settings.m_selectedChannel;
    }
    if (settingsKeys.contains("groupFilter")) {
        m_groupFilter = settings.m_groupFilter;
    }
    if (settingsKeys.contains("title")) {
        m_title = settings.m_title;
    }
    if (settingsKeys.contains("rgbColor")) {
        m_rgbColor = settings.m_rgbColor;
    }
    if (settingsKeys.contains("useReverseAPI")) {
        m_useReverseAPI = settings.m_useReverseAPI;
    }
    if (settingsKeys.contains("reverseAPIAddress")) {
        m_reverseAPIAddress = settings.m_reverseAPIAddress;
    }
    if (settingsKeys.contains("reverseAPIPort")) {
        m_reverseAPIPort = settings.m_reverseAPIPort;
    }
    if (settingsKeys.contains("reverseAPIFeatureSetIndex")) {
        m_reverseAPIFeatureSetIndex = settings.m_reverseAPIFeatureSetIndex;
    }
    if (settingsKeys.contains("reverseAPIFeatureIndex")) {
        m_reverseAPIFeatureIndex = settings.m_reverseAPIFeatureIndex;
    }
    if (settingsKeys.contains("workspaceIndex")) {
        m_workspaceIndex = settings.m_workspaceIndex;
    }
}
