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

#ifndef INCLUDE_SSDVMODFRAMER_H
#define INCLUDE_SSDVMODFRAMER_H

#include <vector>

#include <QByteArray>
#include <QList>

#include "util/ccsds.h"

#include "ssdvmodsettings.h"

// Puts SSDV packets in CCSDS TM frames, and the frames in channel symbols
class SSDVModFramer
{
public:
    SSDVModFramer();

    static CCSDS::CodingSettings getCodingSettings(const SSDVModSettings& settings);

    // symbols: channel symbols (0 or 1) for the preamble, the packets and the tail.
    // progress: for each symbol index where a frame ends, the number of packets sent by then.
    void build(const SSDVModSettings& settings, const QList<QByteArray>& packets,
        std::vector<uint8_t>& symbols, std::vector<std::pair<int, int>>& progress);

    static constexpr int m_idleVirtualChannel = 7;

private:
    CCSDS::FrameEncoder m_encoder;
    int m_masterChannelCount;
    int m_virtualChannelCounts[8];

    QByteArray makeFrame(const SSDVModSettings& settings, int virtualChannel, const QByteArray& payload);
};

#endif // INCLUDE_SSDVMODFRAMER_H
