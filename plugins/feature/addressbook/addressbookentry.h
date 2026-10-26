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

#ifndef INCLUDE_FEATURE_ADDRESSBOOKENTRY_H_
#define INCLUDE_FEATURE_ADDRESSBOOKENTRY_H_

#include <QByteArray>
#include <QString>

struct AddressBookEntry
{
    QString m_name;
    QString m_group;
    qint64 m_frequency;     //!< Receive frequency in Hz
    QString m_mode;         //!< Free text mode, e.g. NFM, USB, SSTV
    QString m_channelType;  //!< Channel plugin ID the entry applies to, e.g. NFMDemod. Empty for any
    int m_bandwidth;        //!< RF bandwidth in Hz. 0 to leave the channel's bandwidth unchanged
    qint64 m_txOffset;      //!< Transmit frequency minus receive frequency in Hz (repeater shift / split)
    float m_ctcss;          //!< CTCSS tone in Hz. 0 for none
    QString m_notes;

    AddressBookEntry();

    void resetToDefaults();
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
};

#endif // INCLUDE_FEATURE_ADDRESSBOOKENTRY_H_
