///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Marcel                                                     //
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

#include <algorithm>

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include "amsatstatus.h"
#include "satnogs.h"

static const char *amsatStatusURL = "https://www.amsat.org/status/";
static const char *amsatStatusAPIURL = "https://www.amsat.org/status/api/v1/sat_info.php";

AMSATStatus::AMSATStatus(QObject *parent) :
    QObject(parent),
    m_enabled(false),
    m_hours(24)
{
    m_networkManager = new QNetworkAccessManager();
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &AMSATStatus::networkManagerFinished);
    // AMSAT reports are in 30 minute slots, so no point polling more often than that
    m_pollTimer.setInterval(30 * 60 * 1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &AMSATStatus::poll);
}

AMSATStatus::~AMSATStatus()
{
    disconnect(m_networkManager, &QNetworkAccessManager::finished, this, &AMSATStatus::networkManagerFinished);
    delete m_networkManager;
}

void AMSATStatus::setEnabled(bool enabled)
{
    if (enabled != m_enabled)
    {
        m_enabled = enabled;
        if (m_enabled)
        {
            m_pollTimer.start();
            poll();
        }
        else
        {
            m_pollTimer.stop();
            m_status.clear();
            emit statusUpdated();
        }
    }
}

void AMSATStatus::setHours(int hours)
{
    if (hours != m_hours)
    {
        m_hours = hours;
        if (m_enabled) {
            requestStatus();
        }
    }
}

void AMSATStatus::setSatellites(const QList<SatNogsSatellite *>& satellites)
{
    QHash<QString, QStringList> names;

    for (const SatNogsSatellite *sat : satellites)
    {
        QStringList satNames = sat->m_names;
        satNames.append(sat->m_name);
        // CelesTrak TLE names, E.g. "FOX-1B (AO-91)"
        if (sat->m_tle)
        {
            static const QRegularExpression re("^(.*?)\\s*(?:\\((.*)\\))?\\s*$");
            QRegularExpressionMatch match = re.match(sat->m_tle->m_tle0.trimmed());
            if (match.hasMatch())
            {
                satNames.append(match.captured(1));
                if (!match.captured(2).isEmpty()) {
                    satNames.append(match.captured(2));
                }
            }
        }
        names.insert(sat->m_name, satNames);
    }

    if (names != m_satelliteNames)
    {
        m_satelliteNames = names;
        if (m_enabled)
        {
            matchEntries();
            requestStatus();
        }
    }
}

QList<AMSATStatus::EntryStatus> AMSATStatus::getStatus(const QString& satellite) const
{
    QList<EntryStatus> list;

    for (const QString& entry : m_satelliteEntries.value(satellite))
    {
        if (m_status.contains(entry)) {
            list.append(m_status.value(entry));
        }
    }
    return list;
}

// Colours used on AMSAT status page
QColor AMSATStatus::backgroundColor(const QString& status)
{
    if (status == "Heard") {
        return QColor(0x64, 0x8f, 0xff);
    } else if (status == "Telemetry Only") {
        return QColor(0xff, 0xb0, 0x00);
    } else if (status == "Not Heard") {
        return QColor(0xdc, 0x26, 0x7f);
    } else if (status == "Conflicting reports") {
        return QColor(0xfe, 0x61, 0x00);
    } else if (status == "Crew Active") {
        return QColor(0x78, 0x5e, 0xf0);
    } else {
        return QColor();
    }
}

QColor AMSATStatus::textColor(const QString& status)
{
    if ((status == "Heard") || (status == "Crew Active")) {
        return Qt::white;
    } else if (backgroundColor(status).isValid()) {
        return Qt::black;
    } else {
        return QColor();
    }
}

// E.g. "35m" or "5h"
QString AMSATStatus::formatAge(const QDateTime& time)
{
    if (!time.isValid()) {
        return "";
    }
    qint64 mins = std::max(0LL, (long long) time.secsTo(QDateTime::currentDateTimeUtc()) / 60);
    if (mins < 60) {
        return QString("%1m").arg(mins);
    } else {
        return QString("%1h").arg(mins / 60);
    }
}

// Remove characters that are used inconsistently in names. E.g. "AO-91", "AO 91", "AO91"
QString AMSATStatus::normalise(const QString& name)
{
    static const QRegularExpression re("[\\s\\-_.]");
    return name.toUpper().remove(re);
}

void AMSATStatus::poll()
{
    if (!m_enabled) {
        return;
    }
    // List of entries rarely changes, so only get it daily
    if (m_entries.isEmpty() || !m_entriesUpdated.isValid() || (m_entriesUpdated.secsTo(QDateTime::currentDateTimeUtc()) > 24 * 60 * 60)) {
        requestEntries();
    } else {
        requestStatus();
    }
}

void AMSATStatus::requestEntries()
{
    QNetworkRequest request(QUrl{amsatStatusURL});
    request.setHeader(QNetworkRequest::UserAgentHeader, "SDRangel Satellite Tracker");
    m_networkManager->get(request);
}

// Get reports for each entry of each satellite
void AMSATStatus::requestStatus()
{
    QStringList entries;
    for (const QStringList& satEntries : m_satelliteEntries) {
        entries.append(satEntries);
    }
    entries.removeDuplicates();

    for (const QString& entry : entries)
    {
        // Build from encoded bytes, as names contain characters that need encoding, E.g. "RS-44_[V/u]"
        // (and QString::arg() would substitute the %2 in an encoded /)
        QUrl url = QUrl::fromEncoded(QByteArray(amsatStatusAPIURL) + "?name=" + QUrl::toPercentEncoding(entry)
            + "&hours=" + QByteArray::number(m_hours));
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, "SDRangel Satellite Tracker");
        request.setAttribute(QNetworkRequest::User, entry);
        m_networkManager->get(request);
    }
}

// Match AMSAT entries to satellites, by name
void AMSATStatus::matchEntries()
{
    m_satelliteEntries.clear();

    for (auto itr = m_satelliteNames.constBegin(); itr != m_satelliteNames.constEnd(); ++itr)
    {
        QStringList normalisedNames;
        for (const QString& name : itr.value()) {
            normalisedNames.append(normalise(name));
        }

        QStringList entries;
        for (const QString& entry : m_entries)
        {
            QString base = entry.section("_[", 0, 0);
            if (normalisedNames.contains(normalise(base))) {
                entries.append(entry);
            }
        }
        if (!entries.isEmpty()) {
            m_satelliteEntries.insert(itr.key(), entries);
        }
    }
}

void AMSATStatus::networkManagerFinished(QNetworkReply *reply)
{
    if (reply->error())
    {
        qWarning() << "AMSATStatus::networkManagerFinished: " << reply->url() << reply->errorString();
    }
    else
    {
        QVariant entry = reply->request().attribute(QNetworkRequest::User);
        if (entry.isValid()) {
            statusReply(reply, entry.toString());
        } else {
            entriesReply(reply);
        }
    }
    reply->deleteLater();
}

// Get list of entries from the satellite selection combo box on the status page. E.g:
// <option value="AO-91_[FM]"
void AMSATStatus::entriesReply(QNetworkReply *reply)
{
    QString html = QString::fromUtf8(reply->readAll());
    static const QRegularExpression re("<option value=\"([^\"]+_\\[[^\"]+\\])\"");
    QRegularExpressionMatchIterator itr = re.globalMatch(html);
    QStringList entries;

    while (itr.hasNext())
    {
        QString entry = itr.next().captured(1);
        if (!entries.contains(entry)) {
            entries.append(entry);
        }
    }

    if (!entries.isEmpty())
    {
        m_entries = entries;
        m_entriesUpdated = QDateTime::currentDateTimeUtc();
        qDebug() << "AMSATStatus::entriesReply:" << m_entries.size() << "entries";
        matchEntries();
        requestStatus();
    }
    else
    {
        qWarning() << "AMSATStatus::entriesReply: No satellites found on AMSAT status page";
    }
}

// Reports are a JSON array of objects, E.g:
// {"name":"AO-91_[FM]","reported_time":"2026-10-21T22:30:00Z","callsign":"VK2MRE","report":"Not Heard","grid_square":"QF56"}
// The AMSAT status page reports conflicting reports in the same time slot, so we do the same for the latest slot
void AMSATStatus::statusReply(QNetworkReply *reply, const QString& entry)
{
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());

    if (!doc.isArray())
    {
        qWarning() << "AMSATStatus::statusReply: Unexpected response for" << entry;
        return;
    }

    QJsonArray array = doc.array();
    EntryStatus status;
    status.m_entry = entry;
    status.m_mode = entry.section("_[", 1).chopped(1).replace('_', ' ');
    status.m_reports = array.size();
    status.m_status = "No reports";
    QStringList latestReports;

    for (const QJsonValue& value : array)
    {
        QJsonObject obj = value.toObject();
        QDateTime time = QDateTime::fromString(obj["reported_time"].toString(), Qt::ISODate);

        if (!status.m_time.isValid() || (time > status.m_time))
        {
            status.m_time = time;
            status.m_callsign = obj["callsign"].toString();
            status.m_grid = obj["grid_square"].toString();
            latestReports = QStringList({obj["report"].toString()});
        }
        else if (time == status.m_time)
        {
            latestReports.append(obj["report"].toString());
        }
    }

    if (!latestReports.isEmpty())
    {
        latestReports.removeDuplicates();
        bool heard = latestReports.contains("Heard") || latestReports.contains("Telemetry Only") || latestReports.contains("Crew Active");
        if (heard && latestReports.contains("Not Heard")) {
            status.m_status = "Conflicting reports";
        } else if (latestReports.contains("Crew Active")) {
            status.m_status = "Crew Active";
        } else if (latestReports.contains("Heard")) {
            status.m_status = "Heard";
        } else {
            status.m_status = latestReports[0];
        }
    }

    m_status.insert(entry, status);
    emit statusUpdated();
}
