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

#ifndef INCLUDE_VARADEMODSETTINGS_H
#define INCLUDE_VARADEMODSETTINGS_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "dsp/dsptypes.h"

class Serializable;

struct VARADemodSettings
{
    qint32 m_inputFrequencyOffset;
    int m_mode; //!< VARAEngine::Mode
    QString m_callsign;
    QString m_address; //!< Listen address of the VARA TCP ports
    int m_cmdPort; //!< VARA command port. Data is cmdPort + 1
    bool m_kissEnabled;
    int m_kissPort;
    Real m_driveDb; //!< Transmit drive, -30 to 0 dB
    int m_rxLagMs; //!< Delay from RF to the modem in ms
    Real m_fmDeviation; //!< FM peak deviation in Hz for full scale modem input
    bool m_agc; //!< AGC in HF modes
    Real m_rxGainDb; //!< Gain in HF modes without AGC
    Real m_volume; //!< Monitor volume
    bool m_audioMute; //!< Monitor audio muted
    QString m_audioDeviceName;
    bool m_linkFrequencyOffset; //!< Keep the offset of linked VARA modulators in sync
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
    bool m_logEnabled; //!< Append the modem's log lines to logFilename
    QString m_logFilename;

    VARADemodSettings();
    void resetToDefaults();
    void setChannelMarker(Serializable *channelMarker) { m_channelMarker = channelMarker; }
    void setRollupState(Serializable *rollupState) { m_rollupState = rollupState; }
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
    void applySettings(const QStringList& settingsKeys, const VARADemodSettings& settings);
    QString getDebugString(const QStringList& settingsKeys, bool force = false) const;
};

#endif /* INCLUDE_VARADEMODSETTINGS_H */
