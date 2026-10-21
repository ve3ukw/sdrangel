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

#ifndef INCLUDE_FEATURE_AMSATSTATUS_H_
#define INCLUDE_FEATURE_AMSATSTATUS_H_

#include <QObject>
#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;
struct SatNogsSatellite;

// Gets satellite status reports from AMSAT's status page: https://www.amsat.org/status/
// Each satellite may have several entries, one per mode, named like "AO-91_[FM]"
class AMSATStatus : public QObject
{
    Q_OBJECT

public:
    // Status of one AMSAT entry (satellite and mode), from the most recent report(s)
    struct EntryStatus {
        QString m_entry;            // E.g. "ISS_[FM]"
        QString m_mode;             // E.g. "FM"
        QString m_status;           // "Heard", "Telemetry Only", "Not Heard", "Crew Active", "Conflicting reports" or "No reports"
        QDateTime m_time;           // Time of most recent report
        QString m_callsign;         // Callsign of most recent report
        QString m_grid;             // Grid square of most recent report
        int m_reports;              // Number of reports in the time window
    };

    AMSATStatus(QObject *parent = nullptr);
    ~AMSATStatus();

    void setEnabled(bool enabled);
    void setHours(int hours);
    void setSatellites(const QList<SatNogsSatellite *>& satellites);    //!< Satellites to get status of
    QList<EntryStatus> getStatus(const QString& satellite) const;       //!< Status of each mode of a satellite, empty if not known to AMSAT

    static QColor backgroundColor(const QString& status);
    static QColor textColor(const QString& status);
    static QString formatAge(const QDateTime& time);

signals:
    void statusUpdated();

private:
    QNetworkAccessManager *m_networkManager;
    QTimer m_pollTimer;
    bool m_enabled;
    int m_hours;
    QStringList m_entries;                          // All entries on AMSAT status page
    QDateTime m_entriesUpdated;
    QHash<QString, QStringList> m_satelliteEntries; // Entries for each satellite we want status of
    QHash<QString, QStringList> m_satelliteNames;   // Names (incl. alternative names) of each satellite
    QHash<QString, EntryStatus> m_status;           // Status of each entry

    void poll();
    void requestEntries();
    void requestStatus();
    void matchEntries();
    void entriesReply(QNetworkReply *reply);
    void statusReply(QNetworkReply *reply, const QString& entry);
    static QString normalise(const QString& name);

private slots:
    void networkManagerFinished(QNetworkReply *reply);
};

#endif // INCLUDE_FEATURE_AMSATSTATUS_H_
