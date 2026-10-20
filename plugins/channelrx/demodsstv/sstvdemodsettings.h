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

#ifndef INCLUDE_SSTVDEMODSETTINGS_H
#define INCLUDE_SSTVDEMODSETTINGS_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "dsp/dsptypes.h"

class Serializable;

struct SSTVDemodSettings
{
    qint32 m_inputFrequencyOffset;
    Real m_rfBandwidth;
    Real m_fmDeviation;         //!< Peak deviation in Hz
    bool m_autoMode;            //!< Select the mode from the VIS code
    int m_mode;                 //!< SSTVMode index used when not in auto mode
    bool m_autoSave;
    QString m_autoSavePath;
    Real m_volume;
    bool m_audioMute;
    QString m_audioDeviceName;
    quint32 m_rgbColor;
    QString m_title;
    Serializable *m_channelMarker;
    int m_streamIndex;
    bool m_useReverseAPI;
    QString m_reverseAPIAddress;
    uint16_t m_reverseAPIPort;
    uint16_t m_reverseAPIDeviceIndex;
    uint16_t m_reverseAPIChannelIndex;
    Serializable *m_rollupState;
    int m_workspaceIndex;
    QByteArray m_geometryBytes;
    bool m_hidden;

    SSTVDemodSettings();
    void resetToDefaults();
    void setChannelMarker(Serializable *channelMarker) { m_channelMarker = channelMarker; }
    void setRollupState(Serializable *rollupState) { m_rollupState = rollupState; }
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
    void applySettings(const QStringList& settingsKeys, const SSTVDemodSettings& settings);
    QString getDebugString(const QStringList& settingsKeys, bool force = false) const;
};

#endif // INCLUDE_SSTVDEMODSETTINGS_H
