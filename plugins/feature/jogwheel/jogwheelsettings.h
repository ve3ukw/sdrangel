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

#ifndef INCLUDE_FEATURE_JOGWHEELSETTINGS_H_
#define INCLUDE_FEATURE_JOGWHEELSETTINGS_H_

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

class Serializable;

struct JogWheelSettings
{
    QString m_title;
    quint32 m_rgbColor;
    QString m_target;           //!< Channel ("R0:1 SSBDemod", offset moved) or device set ("R0", centre frequency moved)
    QString m_linkedTarget;     //!< Moved along with m_target (E.g. satellite uplink). Empty for none
    bool m_linkInverted;        //!< Linked target moves the opposite way (inverting linear transponder)
    int m_stepHz;               //!< Frequency change per detent
    bool m_acceleration;        //!< Larger steps when the wheel is turned quickly
    bool m_udpEnabled;          //!< Listen for jog commands from hardware on UDP
    QString m_udpAddress;
    uint16_t m_udpPort;
    bool m_useReverseAPI;
    QString m_reverseAPIAddress;
    uint16_t m_reverseAPIPort;
    uint16_t m_reverseAPIFeatureSetIndex;
    uint16_t m_reverseAPIFeatureIndex;
    Serializable *m_rollupState;
    int m_workspaceIndex;
    QByteArray m_geometryBytes;

    JogWheelSettings();
    void resetToDefaults();
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
    void setRollupState(Serializable *rollupState) { m_rollupState = rollupState; }
    void applySettings(const QStringList& settingsKeys, const JogWheelSettings& settings);
    QString getDebugString(const QStringList& settingsKeys, bool force=false) const;

    static const QVector<int> m_steps; //!< Step sizes offered in the GUI and cycled through by the UDP "S" command
};

#endif // INCLUDE_FEATURE_JOGWHEELSETTINGS_H_
