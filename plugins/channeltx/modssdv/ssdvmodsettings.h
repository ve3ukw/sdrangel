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

#ifndef INCLUDE_SSDVMODSETTINGS_H
#define INCLUDE_SSDVMODSETTINGS_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "dsp/dsptypes.h"

class Serializable;

struct SSDVModSettings
{
    enum PacketFormat {
        AO123,      //!< 5 byte TM header and a DSLWP SSDV packet in each frame (ASRTU-1 / AO-123, DSLWP)
        Standard    //!< Standard 256 byte SSDV packets, carried as a byte stream in the frames
    };

    static const int m_samplesPerSymbol = 8;

    qint64 m_inputFrequencyOffset;
    int m_baudRate;
    Real m_rrcRolloff; //!< Root raised cosine filter roll-off
    Real m_gain; //!< Output gain in dB
    bool m_channelMute;
    int m_frameSize; //!< CCSDS frame size in bytes, without RS parity
    bool m_rs; //!< Reed-Solomon (255,223)
    bool m_dualBasis; //!< Reed-Solomon in dual basis rather than conventional
    bool m_randomizer; //!< CCSDS pseudo-randomizer
    bool m_differential; //!< Differential coding inside the convolutional code
    bool m_convolutional; //!< Rate 1/2, K=7 convolutional code
    int m_packetFormat; //!< PacketFormat
    QString m_callsign; //!< Callsign in standard SSDV packets
    int m_spacecraftId; //!< TM frame spacecraft ID
    int m_virtualChannel; //!< TM frame virtual channel of image frames
    QString m_imageFilename; //!< Image to send, or directory whose images are sent in turn
    int m_imageId; //!< SSDV image ID of the next image, incremented after each one
    int m_quality; //!< SSDV quality level 0 to 7
    int m_width; //!< Image width, multiple of 16
    int m_height; //!< Image height, multiple of 16
    bool m_fill; //!< Fit the whole image and pad, rather than crop
    int m_preambleMs; //!< Idle frames sent before the image
    bool m_repeat; //!< Send images continuously
    Real m_repeatDelay; //!< Delay between images in seconds
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

    SSDVModSettings();
    void resetToDefaults();
    void setChannelMarker(Serializable *channelMarker) { m_channelMarker = channelMarker; }
    void setRollupState(Serializable *rollupState) { m_rollupState = rollupState; }
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
    void applySettings(const QStringList& settingsKeys, const SSDVModSettings& settings);
    QString getDebugString(const QStringList& settingsKeys, bool force = false) const;
};

#endif /* INCLUDE_SSDVMODSETTINGS_H */
