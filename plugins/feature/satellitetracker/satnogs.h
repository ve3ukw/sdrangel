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

#include <cmath>

#include <QString>
#include <QDateTime>
#include <QDebug>
#include <QTextStream>
#include <QTimeZone>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include "util/csv.h"

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
    QString m_tle1;             // Empty for OMMs that can't be represented as a TLE (catalog number > 99999)
    QString m_tle2;
    QDateTime m_updated;
    QString m_omm;              // For OMMs, elements as a CSV line in the column order of libsgp4's Tle::FromCsv. Empty for TLEs
    QDateTime m_ommEpoch;

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

    SatNogsTLE(const QString& name, int noradCatId, const QDateTime& epoch, const QString& omm, const QString& tle1, const QString& tle2)
    {
        m_noradCatId = noradCatId;
        m_tle0 = name;
        m_tle1 = tle1;
        m_tle2 = tle2;
        m_omm = omm;
        m_ommEpoch = epoch;
    }

    bool isOMM() const { return !m_omm.isEmpty(); }

    // Epoch of the TLE, from line 1 columns 19-32 (YYDDD.DDDDDDDD), or invalid if it can't be parsed
    QDateTime epoch() const
    {
        if (isOMM()) {
            return m_ommEpoch;
        }

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

    // TLE as text, as sent to channels on AOS
    // OMMs that can't be represented as a TLE are sent as the name and CSV line
    QString toString() const
    {
        if (m_tle1.isEmpty() && isOMM()) {
            return m_tle0 + "\n" + m_omm + "\n";
        }
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

    // Check whether a file contains OMMs in CSV format (E.g. CelesTrak FORMAT=csv), rather than TLEs
    static bool isOMMCSV(const QByteArray& array)
    {
        QByteArray start = array.left(64).trimmed();
        if (start.startsWith("\xEF\xBB\xBF")) { // UTF-8 BOM
            start = start.mid(3);
        }
        return start.startsWith("OBJECT_NAME,") || start.startsWith("\"OBJECT_NAME\",");
    }

    // Parse OMMs in CSV format. Columns are found by name in the header, so CelesTrak and Space-Track files can be read
    // Rows with missing or invalid elements are skipped
    static QList<SatNogsTLE *> createListFromOMM(const QByteArray& array)
    {
        QList<SatNogsTLE *> list;
        QByteArray data(array);
        QString error;
        const QStringList required = {
            "OBJECT_NAME", "EPOCH", "MEAN_MOTION", "ECCENTRICITY", "INCLINATION", "RA_OF_ASC_NODE",
            "ARG_OF_PERICENTER", "MEAN_ANOMALY", "NORAD_CAT_ID", "BSTAR", "MEAN_MOTION_DOT", "MEAN_MOTION_DDOT"
        };

        if (data.startsWith("\xEF\xBB\xBF")) { // UTF-8 BOM
            data = data.mid(3);
        }
        data.replace("\r", "");                // CelesTrak uses CRLF line endings
        QTextStream in(&data);
        QHash<QString, int> cols = CSV::readHeader(in, required, error);
        if (!error.isEmpty())
        {
            qDebug() << "SatNogsTLE::createListFromOMM: " << error;
            return list;
        }

        QStringList row;
        while (CSV::readRow(in, &row))
        {
            for (auto& cell : row) {
                cell = cell.trimmed();
            }
            auto field = [&](const QString& name, const QString& def = "") {
                int col = cols.value(name, -1);
                return ((col >= 0) && (col < row.size()) && !row[col].isEmpty()) ? row[col] : def;
            };
            bool ok[13];
            QString name = field("OBJECT_NAME");
            QString objectId = field("OBJECT_ID");
            QDateTime epoch = parseOMMEpoch(field("EPOCH"));
            double meanMotion = field("MEAN_MOTION").toDouble(&ok[0]);
            double eccentricity = field("ECCENTRICITY").toDouble(&ok[1]);
            double inclination = field("INCLINATION").toDouble(&ok[2]);
            double raan = field("RA_OF_ASC_NODE").toDouble(&ok[3]);
            double argPerigee = field("ARG_OF_PERICENTER").toDouble(&ok[4]);
            double meanAnomaly = field("MEAN_ANOMALY").toDouble(&ok[5]);
            int ephemerisType = field("EPHEMERIS_TYPE", "0").toInt(&ok[6]);
            QString classification = field("CLASSIFICATION_TYPE", "U").left(1);
            int noradCatId = field("NORAD_CAT_ID").toInt(&ok[7]);
            int elementSetNo = field("ELEMENT_SET_NO", "999").toInt(&ok[8]);
            int revAtEpoch = field("REV_AT_EPOCH", "0").toInt(&ok[9]);
            double bstar = field("BSTAR").toDouble(&ok[10]);
            double meanMotionDot = field("MEAN_MOTION_DOT").toDouble(&ok[11]);
            double meanMotionDdot = field("MEAN_MOTION_DDOT").toDouble(&ok[12]);

            bool valid = !name.isEmpty() && epoch.isValid() && (noradCatId > 0)
                && (meanMotion > 0.0) && (eccentricity >= 0.0) && (eccentricity < 1.0)
                && (inclination >= 0.0) && (inclination <= 180.0) && (elementSetNo >= 0) && (revAtEpoch >= 0);
            for (int i = 0; valid && (i < 13); i++) {
                valid = ok[i];
            }
            if (!valid)
            {
                qDebug() << "SatNogsTLE::createListFromOMM: Skipping invalid row: " << row;
                continue;
            }

            // libsgp4's CSV parser doesn't support quoting, so remove commas from names
            QString csvName = QString(name).replace(',', ' ');
            QString omm = QStringList({
                csvName, objectId, field("EPOCH"),
                field("MEAN_MOTION"), field("ECCENTRICITY"), field("INCLINATION"), field("RA_OF_ASC_NODE"),
                field("ARG_OF_PERICENTER"), field("MEAN_ANOMALY"), QString::number(ephemerisType), classification,
                QString::number(noradCatId), QString::number(elementSetNo), QString::number(revAtEpoch),
                field("BSTAR"), field("MEAN_MOTION_DOT"), field("MEAN_MOTION_DDOT")
            }).join(',');

            // Also create a TLE, if possible, so channels that receive the TLE on AOS can use it
            QString tle1, tle2;
            if (noradCatId <= 99999)
            {
                QString intDesignator;
                if ((objectId.size() >= 9) && (objectId[4] == '-')) {
                    intDesignator = objectId.mid(2, 2) + objectId.mid(5);   // 1998-067A -> 98067A
                }
                // Use the epoch string, as QDateTime only has ms resolution
                QString epochString = field("EPOCH");
                int dot = epochString.indexOf('.');
                double fraction = (dot >= 0) ? ("0" + epochString.mid(dot)).toDouble() : 0.0;
                QDate date = QDate::fromString(epochString.left(10), "yyyy-MM-dd");
                QTime time = QTime::fromString(epochString.mid(11, 8), "HH:mm:ss");
                double day = date.dayOfYear() + (time.msecsSinceStartOfDay() / 1000.0 + fraction) / 86400.0;

                tle1 = QString("1 %1%2 %3 %4%5 %6 %7 %8 %9 %10")
                    .arg(noradCatId, 5, 10, QChar('0'))
                    .arg(classification)
                    .arg(intDesignator.left(8), -8)
                    .arg(date.year() % 100, 2, 10, QChar('0'))
                    .arg(day, 12, 'f', 8, QChar('0'))
                    .arg(formatTLEDecimal(meanMotionDot))
                    .arg(formatTLEExponent(meanMotionDdot))
                    .arg(formatTLEExponent(bstar))
                    .arg(ephemerisType % 10)
                    .arg(elementSetNo % 10000, 4);
                tle2 = QString("2 %1 %2 %3 %4 %5 %6 %7%8")
                    .arg(noradCatId, 5, 10, QChar('0'))
                    .arg(inclination, 8, 'f', 4)
                    .arg(std::fmod(raan, 360.0), 8, 'f', 4)
                    .arg((int) std::floor(eccentricity * 1e7 + 1e-6), 7, 10, QChar('0'))    // Truncated, as CelesTrak does
                    .arg(std::fmod(argPerigee, 360.0), 8, 'f', 4)
                    .arg(std::fmod(meanAnomaly, 360.0), 8, 'f', 4)
                    .arg(meanMotion, 11, 'f', 8)
                    .arg(revAtEpoch % 100000, 5);
                tle1.append(QString::number(tleChecksum(tle1)));
                tle2.append(QString::number(tleChecksum(tle2)));
            }

            list.append(new SatNogsTLE(name, noradCatId, epoch, omm, tle1, tle2));
        }
        return list;
    }

private:

    // Parse an OMM epoch, which is UTC, with optional fractional seconds. E.g. 2026-10-20T10:01:02.123456
    static QDateTime parseOMMEpoch(const QString& string)
    {
        int dot = string.indexOf('.');
        QDateTime dateTime = QDateTime::fromString(dot >= 0 ? string.left(dot) : string, "yyyy-MM-ddTHH:mm:ss");
        if (!dateTime.isValid()) {
            return dateTime;
        }
        dateTime.setTimeZone(QTimeZone::utc());
        if (dot >= 0) {
            dateTime = dateTime.addMSecs(std::round(("0" + string.mid(dot)).toDouble() * 1000.0));
        }
        return dateTime;
    }

    // Format a value with an assumed leading decimal point. E.g. -0.00001234 -> "-.00001234"
    static QString formatTLEDecimal(double value)
    {
        QString digits = QString::number(std::abs(value), 'f', 8);
        return (value < 0.0 ? "-" : " ") + digits.mid(digits.indexOf('.'));
    }

    // Format a value with an assumed leading decimal point and exponent. E.g. 0.00012345 -> " 12345-3"
    static QString formatTLEExponent(double value)
    {
        double magnitude = std::abs(value);
        int exponent = 0;
        long long mantissa = 0;

        if (magnitude > 0.0)
        {
            exponent = (int) std::floor(std::log10(magnitude)) + 1;
            mantissa = std::llround(magnitude / std::pow(10.0, exponent) * 1e5);
            if (mantissa >= 100000)
            {
                mantissa /= 10;
                exponent++;
            }
            if ((exponent < -9) || (mantissa == 0))
            {
                mantissa = 0;
                exponent = 0;
            }
            else if (exponent > 9)
            {
                mantissa = 99999;
                exponent = 9;
            }
        }
        return QString("%1%2%3%4")
            .arg(((value < 0.0) && (mantissa != 0)) ? "-" : " ")
            .arg(mantissa, 5, 10, QChar('0'))
            .arg(exponent < 0 ? "-" : "+")
            .arg(std::abs(exponent));
    }

    // Modulo 10 checksum of a TLE line: sum of digits, with minus signs counting as 1
    static int tleChecksum(const QString& line)
    {
        int sum = 0;
        for (QChar c : line)
        {
            if (c.isDigit()) {
                sum += c.digitValue();
            } else if (c == '-') {
                sum++;
            }
        }
        return sum % 10;
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
        m_name = tle->m_tle0.trimmed();
        if (match.hasMatch())
        {
            QStringList groups = match.capturedTexts();
            m_name = groups[1].trimmed();
            if ((groups.size() >= 4) && (groups[3] != "-") && !groups[3].isEmpty())
                m_names = QStringList({groups[3].trimmed()});
        }
        m_noradCatId = tle->m_noradCatId;
        m_tle = tle;
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
            if (m_tle->isOMM()) {
                list.append(QString("OMM: %1").arg(m_tle->m_omm));
            }
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
                // TLEs replace each other in the order the files are listed, as they always have
                // When an OMM and a TLE are both available, use whichever has the newest epoch, preferring the TLE on a tie
                bool replace = true;
                if ((m_tle != nullptr) && (m_tle->isOMM() || tle->isOMM()))
                {
                    QDateTime oldEpoch = m_tle->epoch();
                    QDateTime newEpoch = tle->epoch();
                    if (oldEpoch.isValid() && newEpoch.isValid())
                    {
                        // TLE epochs only have a resolution of ~1ms, so allow some tolerance
                        qint64 diff = oldEpoch.msecsTo(newEpoch);
                        replace = (diff > 1000) || ((std::abs(diff) <= 1000) && !tle->isOMM());
                    }
                }
                if (replace) {
                    m_tle = tle;
                }
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
