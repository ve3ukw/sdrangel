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

#ifndef INCLUDE_FEATURE_ADDRESSBOOKIO_H_
#define INCLUDE_FEATURE_ADDRESSBOOKIO_H_

#include <QByteArray>
#include <QList>
#include <QString>

#include "addressbookentry.h"

// Export and import of address book entries, as JSON (all fields), or import from CHIRP CSV
// (as exported by CHIRP, or by RepeaterBook from a search)
class AddressBookIO
{
public:
    static QByteArray toJson(const QList<AddressBookEntry>& entries);
    static bool fromJson(const QByteArray& data, QList<AddressBookEntry>& entries, QString& error);
    static bool fromChirpCsv(const QByteArray& data, const QString& group, QList<AddressBookEntry>& entries, QString& error);

    // Reads JSON or CHIRP CSV, detected from the content. group is used for CHIRP entries, which have no group
    static bool fromData(const QByteArray& data, const QString& group, QList<AddressBookEntry>& entries, QString& error);

    // Appends entries not already in existing (same name and frequency). Returns how many were added
    static int merge(QList<AddressBookEntry>& existing, const QList<AddressBookEntry>& entries);

private:
    static QStringList splitCsvLine(const QString& line);
    static QString channelTypeForMode(const QString& mode);
};

#endif // INCLUDE_FEATURE_ADDRESSBOOKIO_H_
