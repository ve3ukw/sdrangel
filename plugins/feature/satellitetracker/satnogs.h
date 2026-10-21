///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2021-2022 Jon Beniston, M7RCE <jon@beniston.com>                //
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

#ifndef INCLUDE_SATNOGS_H_
#define INCLUDE_SATNOGS_H_

#include <QString>
#include <QDateTime>
#include <QTimeZone>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

struct SatNogsTransmitter {

    int m_noradCatId;           // To link to which satellite this is for
    QString m_description;      // E.g. GMSK9k6 G3RUH AX.25 TLM
    bool m_alive;
    QString m_type;             // "Transmitter", "Trasceiver" or "Transponder"
    qint64 m_uplinkLow;
    qint64 m_uplinkHigh;
    qint64 m_downlinkLow;
    qint64 m_downlinkHigh;
    QString m_mode;             // E.g. "GMSK", "CW", "AFSK", "BPSK", "USB", etc
    int m_baud;
    QString m_status;           // "active", "inactive", "invalid"
    QString m_service;          // "Amateur", "Earth Exploration", "Maritime", "Meteorological", "Mobile", "Space Research"
    bool m_invert;              // Inverting transponder (uplink frequency decreases as downlink increases)
    QString m_uplinkMode;       // E.g. "LSB" for uplink to an inverting SSB transponder
    float m_ctcss;              // Uplink CTCSS tone frequency in Hz, from description, or 0 if none

    SatNogsTransmitter(const QJsonObject& obj)
    {
        m_noradCatId = obj["norad_cat_id"].toInt();
        m_description = obj["description"].toString();
        m_alive = obj["alive"].toBool();
        m_type = obj["type"].toString();
        m_uplinkLow = (qint64)obj["uplink_low"].toDouble();
        m_uplinkHigh = (qint64)obj["uplink_high"].toDouble();
        m_downlinkLow = (qint64)obj["downlink_low"].toDouble();
        m_downlinkHigh = (qint64)obj["downlink_high"].toDouble();
        m_mode = obj["mode"].toString();
        m_baud = obj["baud"].toInt();
        m_status = obj["status"].toString();
        m_service = obj["service"].toString();
        m_invert = obj["invert"].toBool();
        m_uplinkMode = obj["uplink_mode"].toString();
        m_ctcss = parseCTCSS(m_description);
    }

    // CTCSS isn't a SatNOGS field, so look for it in the description
    // E.g. "Voice Repeater CTCSS 67.0 Hz", "FM Voice (PL 88.5Hz)" or "FM 67.0 PL"
    static float parseCTCSS(const QString& description)
    {
        static const QRegularExpression before("\\b(?:CTCSS|PL)\\s*(\\d{2,3}(?:\\.\\d)?)\\s*(?:Hz)?(?![\\d.])", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression after("(\\d{2,3}(?:\\.\\d)?)\\s*(?:Hz)?\\s*(?:CTCSS|PL)\\b", QRegularExpression::CaseInsensitiveOption);

        for (const QRegularExpression& re : {before, after})
        {
            QRegularExpressionMatch match = re.match(description);
            if (match.hasMatch())
            {
                float tone = match.captured(1).toFloat();
                if ((tone >= 67.0f) && (tone <= 254.1f)) {   // Range of standard CTCSS tones
                    return tone;
                }
            }
        }
        return 0.0f;
    }

    static QList<SatNogsTransmitter *> createList(QJsonArray array)
    {
        QList<SatNogsTransmitter *> list;
        for (int i = 0; i < array.size(); i++)
        {
            QJsonValue value = array.at(i);
            if (value.isObject())
                list.append(new SatNogsTransmitter(value.toObject()));
        }

        return list;
    }

    static QString getFrequencyText(quint64 frequency)
    {
        if (frequency > 1000000000)
            return QString("%1 GHz").arg(frequency/1000000000.0, 0, ',', 6);
        else if (frequency > 1000000)
            return QString("%1 MHz").arg(frequency/1000000.0, 0, ',', 3);
        else
            return QString("%1 kHz").arg(frequency/1000.0, 0, ',', 3);
    }

    static QString getFrequencyRangeText(quint64 low, quint64 high)
    {
        if (high > 1000000000)
            return QString("%1-%2 GHz").arg(low/1000000000.0, 0, ',', 6).arg(high/1000000000.0, 0, ',', 6);
        else if (high > 1000000)
            return QString("%1-%2 MHz").arg(low/1000000.0, 0, ',', 3).arg(high/1000000.0, 0, ',', 3);
        else
            return QString("%1-%2 kHz").arg(low/1000.0, 0, ',', 3).arg(high/1000.0, 0, ',', 3);
    }

};

struct SatNogsTLE {

    int m_noradCatId;           // To link to which satellite this is for
    QString m_tle0;
    QString m_tle1;
    QString m_tle2;
    QDateTime m_updated;

    SatNogsTLE(const QJsonObject &obj)
    {
        m_noradCatId = obj["norad_cat_id"].toInt();
        m_tle0 = obj["tle0"].toString();
        m_tle1 = obj["tle1"].toString();
        m_tle2 = obj["tle2"].toString();
        m_updated = QDateTime::fromString(obj["updated"].toString(), Qt::ISODateWithMs);
    }

    SatNogsTLE(const QString& tle0, const QString& tle1, const QString& tle2)
    {
        m_noradCatId = tle2.mid(2, 5).toInt();
        m_tle0 = tle0;
        m_tle1 = tle1;
        m_tle2 = tle2;
    }

    // Epoch of the TLE, from line 1 columns 19-32 (YYDDD.DDDDDDDD), or invalid if it can't be parsed
    QDateTime epoch() const
    {
        bool yearOk, dayOk;
        int year = m_tle1.mid(18, 2).toInt(&yearOk);
        double day = m_tle1.mid(20, 12).toDouble(&dayOk);

        if (!yearOk || !dayOk) {
            return QDateTime();
        }
        year += (year < 57) ? 2000 : 1900;
        QDateTime start(QDate(year, 1, 1), QTime(0, 0), QTimeZone::utc());
        return start.addMSecs((qint64) ((day - 1.0) * 86400000.0));
    }

    // Age of the TLE in days, or -1 if unknown
    int ageInDays() const
    {
        QDateTime e = epoch();
        return e.isValid() ? e.daysTo(QDateTime::currentDateTimeUtc()) : -1;
    }

    QString toString() const
    {
        return m_tle0 + "\n" + m_tle1 + "\n" + m_tle2 + "\n";
    }

    static QList<SatNogsTLE *> createList(QJsonArray array)
    {
        QList<SatNogsTLE *> list;
        for (int i = 0; i < array.size(); i++)
        {
            QJsonValue value = array.at(i);
            if (value.isObject())
                 list.append(new SatNogsTLE(value.toObject()));
        }

        return list;
    }

    // Check a pair of lines look like TLE lines 1 and 2 for the same satellite
    static bool isValidTLE(const QString& tle1, const QString& tle2)
    {
        return (tle1.size() >= 69) && (tle2.size() >= 69)
            && tle1.startsWith("1 ") && tle2.startsWith("2 ")
            && (tle1.mid(2, 5) == tle2.mid(2, 5));
    }

    // Parse a text file of 3-line TLEs (name, line 1, line 2)
    // Lines that aren't part of a valid TLE (E.g. an HTML error page or another file format) are skipped
    static QList<SatNogsTLE *> createList(const QByteArray& array)
    {
        QList<SatNogsTLE *> list;
        QList<QByteArray> lines = array.split('\n');
        int i = 0;
        while (i + 2 < lines.size())
        {
            QString tle0 = QString(lines[i]).trimmed();
            QString tle1 = QString(lines[i+1]).trimmed();
            QString tle2 = QString(lines[i+2]).trimmed();
            if (!tle0.isEmpty() && isValidTLE(tle1, tle2))
            {
                list.append(new SatNogsTLE(tle0, tle1, tle2));
                i += 3;
            }
            else
            {
                i++;
            }
        }
        return list;
    }
};

struct SatNogsSatellite {

    int m_noradCatId;
    QString m_name;
    QStringList m_names;        // Alterantive names - JSON "AO-10\r\nOSCAR-10"
    QStringList m_tleSources;   // Names of TLE files (other than SatNOGS) that include this satellite. E.g. "weather" for CelesTrak GROUP=weather
    QString m_image;            // URL to image of satellie - JSON example: "https://db-satnogs.freetls.fastly.net/media/satellites/sigma.jpg"
    QString m_status;           // "in orbit" (formerly "alive") "re-entered" "dead" "future"  or "" for TLE only sats
    QDateTime m_decayed;        // Date of decay. JSON "2018-05-19T00:00:00Z"
    QDateTime m_launched;
    QDateTime m_deployed;
    QString m_website;
    QString m_operator;         // "None" or "European Space Agency",
    QString m_countries;        // "US" or "ES"

    QList<SatNogsTransmitter *> m_transmitters;
    SatNogsTLE *m_tle;

    SatNogsSatellite(const QJsonObject& obj)
    {
        m_noradCatId = obj["norad_cat_id"].toInt();
        m_name = obj["name"].toString();
        m_names = obj["names"].toString().split("\r\n");
        if ((m_names.size() == 1) && m_names[0].isEmpty())
            m_names = QStringList();
        m_image = obj["image"].toString();
        m_status = obj["status"].toString();
        if (!obj["decayed"].isNull())
            m_decayed = QDateTime::fromString(obj["decayed"].toString(), Qt::ISODate);
        if (!obj["launched"].isNull())
            m_launched = QDateTime::fromString(obj["launched"].toString(), Qt::ISODate);
        if (!obj["deployed"].isNull())
            m_deployed = QDateTime::fromString(obj["deployed"].toString(), Qt::ISODate);
        m_website = obj["website"].toString();
        m_operator = obj["operator"].toString();
        m_countries = obj["countries"].toString();
        m_tle = nullptr;
    }

    SatNogsSatellite(SatNogsTLE *tle)
    {
        // Extract names from TLE
        // tle0 is of the form:
        //   MOZHAYETS 4 (RS-22)
        //   GOES 9 [-]
        QRegularExpression re("([A-Za-z0-9\\- ]+)([\\(]([A-Z0-9\\- ]+)[\\)])?");
        QRegularExpressionMatch match = re.match(tle->m_tle0);
        if (match.hasMatch())
        {
            QStringList groups = match.capturedTexts();
            m_name = groups[1].trimmed();
            if ((groups.size() >= 4) && (groups[3] != "-") && !groups[3].isEmpty())
                m_names = QStringList({groups[3].trimmed()});
            m_noradCatId = tle->m_tle2.mid(2, 5).toInt();
            m_tle = tle;
        }
    }

    QString toString()
    {
        QStringList list;
        list.append(QString("Name: %1").arg(m_name));
        list.append(QString("NORAD ID: %1").arg(m_noradCatId));
        if (m_tle != nullptr)
        {
            list.append(QString("TLE0: %1").arg(m_tle->m_tle0));
            list.append(QString("TLE1: %1").arg(m_tle->m_tle1));
            list.append(QString("TLE2: %1").arg(m_tle->m_tle2));
        }
        for (int i = 0; i < m_transmitters.size(); i++)
        {
            list.append(QString("Mode: %1 Freq: %2").arg(m_transmitters[i]->m_mode).arg(m_transmitters[i]->m_downlinkLow));
        }
        return list.join("\n");
    }

    void addTransmitters(const QList<SatNogsTransmitter *>& transmitters)
    {
        for (int i = 0; i < transmitters.size(); i++)
        {
            SatNogsTransmitter *tx = transmitters[i];
            if (tx->m_noradCatId == m_noradCatId)
                m_transmitters.append(tx);
        }
    }

    void addTLE(const QList<SatNogsTLE *>& tles, const QString& source = "")
    {
        for (int i = 0; i < tles.size(); i++)
        {
            SatNogsTLE *tle = tles[i];
            if (tle->m_noradCatId == m_noradCatId)
            {
                m_tle = tle;
                if (!source.isEmpty() && !m_tleSources.contains(source)) {
                    m_tleSources.append(source);
                }
            }
        }
    }

    // Create a hash table of satellites from the JSON object
    static QHash<QString, SatNogsSatellite *> createHash(QJsonArray array)
    {
        QHash<QString, SatNogsSatellite *> hash;

        for (int i = 0; i < array.size(); i++)
        {
            QJsonValue value = array.at(i);
            if (value.isObject())
            {
                 SatNogsSatellite *sat = new SatNogsSatellite(value.toObject());
                 hash.insert(sat->m_name, sat);
            }
        }
        return hash;
    }

};


#endif // INCLUDE_SATNOGS_H_
