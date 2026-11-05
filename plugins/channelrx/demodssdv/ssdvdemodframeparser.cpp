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

#include "util/ccsds.h"

#include "ssdvdemodframeparser.h"

SSDVDemodFrameParser::SSDVDemodFrameParser() :
    m_packetFormat(SSDVDemodSettings::AO123)
{
}

void SSDVDemodFrameParser::setPacketFormat(int packetFormat)
{
    if (packetFormat != m_packetFormat)
    {
        m_packetFormat = packetFormat;
        m_stream.clear();
    }
}

QList<SSDVDemodFrameParser::Packet> SSDVDemodFrameParser::parse(const QByteArray& frame)
{
    QList<Packet> packets;
    SSDV::PacketInfo info;
    QByteArray standardPacket;

    // Frames start with a TM primary header in its short form
    QByteArray payload = frame.mid(CCSDS::TMShortHeader::m_size);

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
