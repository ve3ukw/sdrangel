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

#ifndef INCLUDE_SSDVDEMODFRAMEPARSER_H
#define INCLUDE_SSDVDEMODFRAMEPARSER_H

#include <QByteArray>
#include <QList>

#include "util/ssdvcodec.h"

#include "ssdvdemodsettings.h"

// Finds the SSDV packets in CCSDS TM frames
class SSDVDemodFrameParser
{
public:
    struct Packet
    {
        QByteArray m_data;
        SSDV::Variant m_variant;
    };

    SSDVDemodFrameParser();
    void setPacketFormat(int packetFormat);
    void reset() { m_stream.clear(); }
    // Returns valid packets that the frame holds or completes
    QList<Packet> parse(const QByteArray& frame);

private:
    int m_packetFormat;
    QByteArray m_stream; //!< Frame data, for standard packets that span frames
};

#endif // INCLUDE_SSDVDEMODFRAMEPARSER_H
