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

#include "util/simpleserializer.h"

#include "addressbookentry.h"

AddressBookEntry::AddressBookEntry()
{
    resetToDefaults();
}

void AddressBookEntry::resetToDefaults()
{
    m_name.clear();
    m_group.clear();
    m_frequency = 0;
    m_mode.clear();
    m_channelType.clear();
    m_bandwidth = 0;
    m_txOffset = 0;
    m_ctcss = 0.0f;
    m_notes.clear();
}

QByteArray AddressBookEntry::serialize() const
{
    SimpleSerializer s(1);

    s.writeString(1, m_name);
    s.writeString(2, m_group);
    s.writeS64(3, m_frequency);
    s.writeString(4, m_mode);
    s.writeString(5, m_channelType);
    s.writeS32(6, m_bandwidth);
    s.writeS64(7, m_txOffset);
    s.writeFloat(8, m_ctcss);
    s.writeString(9, m_notes);

    return s.final();
}

bool AddressBookEntry::deserialize(const QByteArray& data)
{
    SimpleDeserializer d(data);

    if (!d.isValid())
    {
        resetToDefaults();
        return false;
    }

    if (d.getVersion() == 1)
    {
        d.readString(1, &m_name, "");
        d.readString(2, &m_group, "");
        d.readS64(3, &m_frequency, 0);
        d.readString(4, &m_mode, "");
        d.readString(5, &m_channelType, "");
        d.readS32(6, &m_bandwidth, 0);
        d.readS64(7, &m_txOffset, 0);
        d.readFloat(8, &m_ctcss, 0.0f);
        d.readString(9, &m_notes, "");

        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}
