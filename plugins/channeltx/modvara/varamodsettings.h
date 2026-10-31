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

#ifndef INCLUDE_VARAMODSETTINGS_H
#define INCLUDE_VARAMODSETTINGS_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "dsp/dsptypes.h"

class Serializable;

struct VARAModSettings
{
    qint64 m_inputFrequencyOffset;
    int m_cmdPort; //!< Command port of the VARA Demod whose modem is transmitted
    Real m_fmDeviation; //!< FM peak deviation in Hz for full scale modem output
    Real m_gain; //!< Output gain in dB
    bool m_channelMute; //!< Also stops the modem from keying
    bool m_pttEnabled; //!< Key a UDP PTT sequencer
    QString m_pttAddress;
    int m_pttPort;
    int m_pttLeadMs; //!< From keying PTT to the first sample reaching the modulator
    int m_pttTailMs; //!< PTT held after the last sample
    int m_txPathMs; //!< Delay from the modulator to RF in ms
    bool m_linkFrequencyOffset; //!< Keep the offset of linked VARA demodulators in sync
    quint32 m_rgbColor;
    QString m_title;
    Serializable *m_channelMarker;
    int m_streamIndex; //!< MIMO channel. Not relevant when connected to SI (single Rx).
    bool m_useReverseAPI;
    QString m_reverseAPIAddress;
    uint16_t m_reverseAPIPort;
    uint16_t m_reverseAPIDeviceIndex;
    uint16_t m_reverseAPIChannelIndex;
    Serializable *m_rollupState;
    int m_workspaceIndex;
    QByteArray m_geometryBytes;
    bool m_hidden;

    VARAModSettings();
    void resetToDefaults();
    void setChannelMarker(Serializable *channelMarker) { m_channelMarker = channelMarker; }
    void setRollupState(Serializable *rollupState) { m_rollupState = rollupState; }
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
    void applySettings(const QStringList& settingsKeys, const VARAModSettings& settings);
    QString getDebugString(const QStringList& settingsKeys, bool force = false) const;
};

#endif /* INCLUDE_VARAMODSETTINGS_H */
