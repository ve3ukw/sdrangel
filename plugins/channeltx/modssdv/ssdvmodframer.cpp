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

#include <cmath>

#include "ssdvmodframer.h"

// Bits after the last frame, so the receiver's Viterbi decoder outputs all of it
static constexpr int m_tailBits = 2 * CCSDS::ViterbiDecoder::m_tracebackLength;

SSDVModFramer::SSDVModFramer() :
    m_masterChannelCount(0)
{
    std::fill(m_virtualChannelCounts, m_virtualChannelCounts + 8, 0);
}

CCSDS::CodingSettings SSDVModFramer::getCodingSettings(const SSDVModSettings& settings)
{
    CCSDS::CodingSettings coding;
    coding.m_frameSize = std::max(CCSDS::TMShortHeader::m_size + 1,
        std::min(settings.m_rs ? CCSDS::m_rsMaxData : 1024, settings.m_frameSize));
    coding.m_rs = settings.m_rs;
    coding.m_dualBasis = settings.m_dualBasis;
    coding.m_randomizer = settings.m_randomizer;
    coding.m_differential = settings.m_differential;
    coding.m_convolutional = settings.m_convolutional;
    return coding;
}

QByteArray SSDVModFramer::makeFrame(const SSDVModSettings& settings, int virtualChannel, const QByteArray& payload)
{
    int frameSize = m_encoder.getSettings().m_frameSize;
    QByteArray frame(frameSize, (char) 0x55);
    CCSDS::TMShortHeader header;
    header.m_spacecraftId = settings.m_spacecraftId;
    header.m_virtualChannel = virtualChannel;
    header.m_masterChannelCount = m_masterChannelCount;
    header.m_virtualChannelCount = m_virtualChannelCounts[virtualChannel & 7];
    header.m_firstHeaderPointer = 0;
    header.encode((uint8_t *) frame.data());
    m_masterChannelCount = (m_masterChannelCount + 1) & 0xFF;
    m_virtualChannelCounts[virtualChannel & 7] = (m_virtualChannelCounts[virtualChannel & 7] + 1) & 0xFF;

    int size = std::min((int) payload.size(), frameSize - CCSDS::TMShortHeader::m_size);
    std::copy(payload.constBegin(), payload.constBegin() + size, frame.begin() + CCSDS::TMShortHeader::m_size);

    return frame;
}

void SSDVModFramer::build(const SSDVModSettings& settings, const QList<QByteArray>& packets,
    std::vector<uint8_t>& symbols, std::vector<std::pair<int, int>>& progress)
{
    m_encoder.setSettings(getCodingSettings(settings));
    m_encoder.reset();
    symbols.clear();
    progress.clear();

    int frameSize = m_encoder.getSettings().m_frameSize;
    int payloadSize = frameSize - CCSDS::TMShortHeader::m_size;
    int symbolsPerFrame = (CCSDS::m_asmBits + m_encoder.getSettings().codewordSize() * 8)
        * (m_encoder.getSettings().m_convolutional ? 2 : 1);

    // Idle frames to let the receiver lock
    int preambleFrames = std::max(1, (int) std::ceil(settings.m_preambleMs / 1000.0 * settings.m_baudRate / symbolsPerFrame));

    for (int i = 0; i < preambleFrames; i++) {
        m_encoder.encodeFrame(makeFrame(settings, m_idleVirtualChannel, QByteArray()), symbols);
    }

    if (settings.m_packetFormat == SSDVModSettings::AO123)
    {
        // One packet per frame
        for (int i = 0; i < packets.size(); i++)
        {
            m_encoder.encodeFrame(makeFrame(settings, settings.m_virtualChannel, packets[i]), symbols);
            progress.push_back({(int) symbols.size(), i + 1});
        }
    }
    else
    {
        // Packets as a byte stream
        QByteArray stream;

        for (const auto& packet : packets) {
            stream.append(packet);
        }

        int packetSize = packets.isEmpty() ? 1 : packets.first().size();

        for (int offset = 0; offset < stream.size(); offset += payloadSize)
        {
            m_encoder.encodeFrame(makeFrame(settings, settings.m_virtualChannel, stream.mid(offset, payloadSize)), symbols);
            int sent = std::min((int) packets.size(), (offset + payloadSize) / packetSize);
            progress.push_back({(int) symbols.size(), sent});
        }
    }

    // Flush the receiver's decoder
    std::vector<uint8_t> tail(m_tailBits, 0);
    m_encoder.encodeBits(tail.data(), tail.size(), symbols);

    if (!progress.empty()) {
        progress.back().first = symbols.size();
    }
}
