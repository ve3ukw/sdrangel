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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include "addressbookio.h"

QByteArray AddressBookIO::toJson(const QList<AddressBookEntry>& entries)
{
    QJsonArray array;

    for (const auto& entry : entries)
    {
        QJsonObject obj;
        obj["name"] = entry.m_name;
        obj["group"] = entry.m_group;
        obj["frequency"] = entry.m_frequency;
        obj["mode"] = entry.m_mode;
        obj["channelType"] = entry.m_channelType;
        obj["bandwidth"] = entry.m_bandwidth;
        obj["txOffset"] = entry.m_txOffset;
        // Tones are to 0.1 Hz. Round via text, so float rounding errors aren't written (dividing isn't exact with -ffast-math)
        obj["ctcss"] = QString::number(entry.m_ctcss, 'f', 1).toDouble();
        obj["azimuth"] = entry.m_azimuth;
        obj["notes"] = entry.m_notes;
        array.append(obj);
    }

    QJsonObject root;
    root["sdrangelAddressBook"] = 1;
    root["entries"] = array;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool AddressBookIO::fromJson(const QByteArray& data, QList<AddressBookEntry>& entries, QString& error)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (doc.isNull())
    {
        error = QString("Invalid JSON: %1").arg(parseError.errorString());
        return false;
    }

    // An exported file, or just the array of entries
    QJsonArray array = doc.isArray() ? doc.array() : doc.object().value("entries").toArray();

    if (!doc.isArray() && !doc.object().contains("entries"))
    {
        error = "JSON has no entries";
        return false;
    }

    for (const auto& value : array)
    {
        QJsonObject obj = value.toObject();
        AddressBookEntry entry;
        entry.m_name = obj.value("name").toString();
        entry.m_group = obj.value("group").toString();
        entry.m_frequency = (qint64) obj.value("frequency").toDouble();
        entry.m_mode = obj.value("mode").toString();
        entry.m_channelType = obj.value("channelType").toString();
        entry.m_bandwidth = obj.value("bandwidth").toInt();
        entry.m_txOffset = (qint64) obj.value("txOffset").toDouble();
        entry.m_ctcss = (float) obj.value("ctcss").toDouble();
        entry.m_azimuth = (float) obj.value("azimuth").toDouble(-1.0);
        entry.m_notes = obj.value("notes").toString();

        if (entry.m_frequency > 0) {
            entries.append(entry);
        }
    }

    return true;
}

// CHIRP CSV columns, as written by CHIRP and by RepeaterBook's "CHIRP" export:
// Location,Name,Frequency,Duplex,Offset,Tone,rToneFreq,cToneFreq,DtcsCode,DtcsPolarity,RxDtcsCode,CrossMode,Mode,TStep,Skip,Power,Comment,...
// Frequency and Offset are in MHz. Duplex is +, - (Offset is the shift), split (Offset is the transmit frequency) or off (no transmit).
// Tone is empty, Tone (rToneFreq on transmit), TSQL (cToneFreq on transmit and receive), DTCS or Cross (CrossMode, E.g. Tone->Tone)
bool AddressBookIO::fromChirpCsv(const QByteArray& data, const QString& group, QList<AddressBookEntry>& entries, QString& error)
{
    QStringList lines = QString::fromUtf8(data).split(QRegularExpression("\r\n|\n|\r"), Qt::SkipEmptyParts);

    if (lines.isEmpty())
    {
        error = "File is empty";
        return false;
    }

    QStringList header = splitCsvLine(lines[0]);

    for (auto& column : header) {
        column = column.trimmed().toLower();
    }

    int frequencyCol = header.indexOf("frequency");

    if (frequencyCol < 0)
    {
        error = "Not a CHIRP CSV file (no Frequency column)";
        return false;
    }

    int nameCol = header.indexOf("name");
    int duplexCol = header.indexOf("duplex");
    int offsetCol = header.indexOf("offset");
    int toneCol = header.indexOf("tone");
    int rToneCol = header.indexOf("rtonefreq");
    int cToneCol = header.indexOf("ctonefreq");
    int dtcsCol = header.indexOf("dtcscode");
    int crossCol = header.indexOf("crossmode");
    int modeCol = header.indexOf("mode");
    int commentCol = header.indexOf("comment");

    for (int i = 1; i < lines.size(); i++)
    {
        QStringList fields = splitCsvLine(lines[i]);
        auto field = [&fields](int col) { return (col >= 0) && (col < fields.size()) ? fields[col].trimmed() : QString(); };

        bool ok;
        double frequencyMHz = field(frequencyCol).toDouble(&ok);

        if (!ok || (frequencyMHz <= 0.0)) {
            continue;
        }

        AddressBookEntry entry;
        entry.m_group = group;
        entry.m_frequency = qRound64(frequencyMHz * 1e6);
        entry.m_notes = field(commentCol);
        entry.m_name = field(nameCol);

        if (entry.m_name.isEmpty()) {
            entry.m_name = entry.m_notes.isEmpty() ? QString("%1 MHz").arg(frequencyMHz, 0, 'f', 4) : entry.m_notes.section(',', 0, 0);
        }

        QString duplex = field(duplexCol).toLower();
        qint64 offset = qRound64(field(offsetCol).toDouble() * 1e6);

        if (duplex == "+") {
            entry.m_txOffset = offset;
        } else if (duplex == "-") {
            entry.m_txOffset = -offset;
        } else if ((duplex == "split") && (offset > 0)) {
            entry.m_txOffset = offset - entry.m_frequency;
        } else if (duplex == "off") {
            entry.m_notes = entry.m_notes.isEmpty() ? QString("Receive only") : entry.m_notes + " (receive only)";
        }

        QString tone = field(toneCol);
        QString crossMode = field(crossCol);

        if (tone == "Tone") {
            entry.m_ctcss = field(rToneCol).toFloat();
        } else if (tone == "TSQL") {
            entry.m_ctcss = field(cToneCol).toFloat();
        } else if ((tone == "Cross") && crossMode.startsWith("Tone->")) {
            entry.m_ctcss = field(rToneCol).toFloat();
        } else if ((tone == "DTCS") || ((tone == "Cross") && crossMode.startsWith("DTCS"))) {
            // DCS isn't supported by the modulators, so note it
            QString dcs = QString("DCS %1").arg(field(dtcsCol));
            entry.m_notes = entry.m_notes.isEmpty() ? dcs : entry.m_notes + ", " + dcs;
        }

        // CHIRP's FM is 5 kHz deviation narrowband FM, which is SDRangel's NFM
        QString mode = field(modeCol).toUpper();

        if ((mode == "FM") || (mode == "NFM") || mode.isEmpty()) {
            mode = "NFM";
        } else if (mode == "DV") {
            mode = "D-STAR";
        } else if (mode == "DN") {
            mode = "C4FM";
        }

        entry.m_mode = mode;
        entry.m_channelType = channelTypeForMode(mode);
        entries.append(entry);
    }

    return true;
}

bool AddressBookIO::fromData(const QByteArray& data, const QString& group, QList<AddressBookEntry>& entries, QString& error)
{
    QByteArray trimmed = data.trimmed();

    if (trimmed.startsWith('{') || trimmed.startsWith('[')) {
        return fromJson(data, entries, error);
    } else {
        return fromChirpCsv(data, group, entries, error);
    }
}

int AddressBookIO::merge(QList<AddressBookEntry>& existing, const QList<AddressBookEntry>& entries)
{
    int added = 0;

    for (const auto& entry : entries)
    {
        bool duplicate = false;

        for (const auto& e : existing)
        {
            if ((e.m_name == entry.m_name) && (e.m_frequency == entry.m_frequency))
            {
                duplicate = true;
                break;
            }
        }

        if (!duplicate)
        {
            existing.append(entry);
            added++;
        }
    }

    return added;
}

// Split a CSV line, allowing for quoted fields containing commas and doubled quotes
QStringList AddressBookIO::splitCsvLine(const QString& line)
{
    QStringList fields;
    QString field;
    bool quoted = false;

    for (int i = 0; i < line.size(); i++)
    {
        QChar c = line[i];

        if (quoted)
        {
            if ((c == '"') && (i + 1 < line.size()) && (line[i + 1] == '"'))
            {
                field.append('"');
                i++;
            }
            else if (c == '"')
            {
                quoted = false;
            }
            else
            {
                field.append(c);
            }
        }
        else if (c == '"')
        {
            quoted = true;
        }
        else if (c == ',')
        {
            fields.append(field);
            field.clear();
        }
        else
        {
            field.append(c);
        }
    }

    fields.append(field);
    return fields;
}

QString AddressBookIO::channelTypeForMode(const QString& mode)
{
    if (mode == "NFM") {
        return "NFMDemod";
    } else if (mode == "WFM") {
        return "WFMDemod";
    } else if (mode == "AM") {
        return "AMDemod";
    } else if ((mode == "USB") || (mode == "LSB") || (mode == "CW")) {
        return "SSBDemod";
    } else {
        return ""; // Any
    }
}
