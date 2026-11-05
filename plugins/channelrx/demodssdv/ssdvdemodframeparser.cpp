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

#include <algorithm>

#include "util/ccsds.h"

#include "ssdvdemoddeframers.h"
#include "ssdvdemodframeparser.h"

SSDVDemodFrameParser::SSDVDemodFrameParser() :
    m_packetFormat(SSDVDemodSettings::AO123),
    m_framing(SSDVDemodSettings::CCSDS)
{
}

void SSDVDemodFrameParser::setFormat(int packetFormat, int framing)
{
    if ((packetFormat != m_packetFormat) || (framing != m_framing))
    {
        m_packetFormat = packetFormat;
        m_framing = framing;
        m_stream.clear();
    }
}

// Information field of an AX.25 frame (without CRC), or empty if it isn't one
QByteArray SSDVDemodFrameParser::ax25Info(const QByteArray& frame)
{
    // Addresses are 7 bytes each, the last with bit 0 of its last byte set, followed by control and PID
    for (int i = 6; i < std::min((int) frame.size(), 7 * 10); i += 7)
    {
        if (frame[i] & 1)
        {
            int start = i + 3;
            return (i >= 13) && (start <= frame.size()) ? frame.mid(start) : QByteArray();
        }
    }

    return QByteArray();
}

QList<SSDVDemodFrameParser::Packet> SSDVDemodFrameParser::parse(const QByteArray& frame)
{
    QList<Packet> packets;
    SSDV::PacketInfo info;
    QByteArray standardPacket;

    if (m_framing == SSDVDemodSettings::Genesis)
    {
        // SSDV packets are sent without their sync and type bytes, with the satellite's frame header
        // (sync word, size and type / address) in place of the callsign
        if ((frame.size() == SSDVGenesisDeframer::m_ssdvSize) && ((((quint8) frame[0]) >> 4) == SSDVGenesisDeframer::m_ssdvType))
        {
            QByteArray data = QByteArray::fromHex("5566BF35FB") + frame;

            if (SSDV::parsePacket(data, SSDV::Standard, info, standardPacket)) {
                packets.append(Packet{data, SSDV::Standard});
            }
        }

        return packets;
    }

    // CCSDS frames start with a TM primary header in its short form. USP frames are AX.25 frames.
    QByteArray payload = m_framing == SSDVDemodSettings::USP ? ax25Info(frame) : frame.mid(CCSDS::TMShortHeader::m_size);

    if (m_packetFormat == SSDVDemodSettings::AO123)
    {
        // One packet straight after the header
        QByteArray data = payload.left(SSDV::m_dslwpPacketSize);

        if ((data.size() == SSDV::m_dslwpPacketSize) && SSDV::parsePacket(data, SSDV::DSLWP, info, standardPacket)) {
            packets.append(Packet{data, SSDV::DSLWP});
        }
    }
    else
    {
        // Packets carried as a byte stream
        m_stream.append(payload);

        while (m_stream.size() >= SSDV::m_standardPacketSize)
        {
            QByteArray candidate = m_stream.left(SSDV::m_standardPacketSize);

            if (((quint8) candidate[0] == 0x55) && SSDV::parsePacket(candidate, SSDV::Standard, info, standardPacket))
            {
                packets.append(Packet{candidate, SSDV::Standard});
                m_stream.remove(0, SSDV::m_standardPacketSize);
            }
            else
            {
                m_stream.remove(0, 1);
            }
        }
    }

    return packets;
}
