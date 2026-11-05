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
#include <bitset>

#include "ssdvdemoddeframers.h"

// Most collectors are started by noise, and are dropped when a frame can't be decoded
static constexpr int m_maxCollectors = 8;

// USP sync word, and the PLS codes (after the PLS scrambler) for the two frame lengths
static constexpr uint64_t m_uspSync = 0x5072F64B2D90B1F5ULL;
static constexpr int m_uspSyncBits = 64;
static constexpr int m_uspPlsSymbols = 64;
static constexpr uint64_t m_uspPls[2] = {0x719D83C953422DFAULL, 0x24C8D69C061778AFULL};
static constexpr int m_uspFrameSize[2] = {48, 223};
static constexpr int m_uspHeaderSize = 4;

SSDVUSPDeframer::SSDVUSPDeframer()
{
    reset();
}

void SSDVUSPDeframer::reset()
{
    m_sr = 0;
    m_count = 0;
    m_collectors.clear();
    m_framesBad = 0;
}

void SSDVUSPDeframer::process(float symbol, QList<CCSDS::DecodedFrame>& frames)
{
    for (auto it = m_collectors.begin(); it != m_collectors.end();)
    {
        Collector& collector = *it;
        collector.m_symbols.push_back(collector.m_invert ? -symbol : symbol);

        if ((int) collector.m_symbols.size() == m_uspPlsSymbols)
        {
            // Correlate with the two PLS codes
            float correlation[2] = {0.0f, 0.0f};

            for (int i = 0; i < m_uspPlsSymbols; i++)
            {
                for (int code = 0; code < 2; code++)
                {
                    int bit = (m_uspPls[code] >> (m_uspPlsSymbols - 1 - i)) & 1;
                    correlation[code] += bit ? collector.m_symbols[i] : -collector.m_symbols[i];
                }
            }

            int code = correlation[1] > correlation[0] ? 1 : 0;
            collector.m_needed = m_uspPlsSymbols + (m_uspFrameSize[code] + CCSDS::m_rsParity) * 16;
        }

        if ((int) collector.m_symbols.size() >= collector.m_needed)
        {
            CCSDS::DecodedFrame frame;

            if (finishFrame(collector, frame)) {
                frames.append(frame);
            }

            it = m_collectors.erase(it);
        }
        else
        {
            ++it;
        }
    }

    m_sr = (m_sr << 1) | (symbol > 0.0f ? 1 : 0);

    if (m_count < m_uspSyncBits)
    {
        m_count++;
        return;
    }

    int errors = std::bitset<64>(m_sr ^ m_uspSync).count();
    bool invert = errors > m_uspSyncBits / 2;

    if (invert) {
        errors = m_uspSyncBits - errors;
    }

    if ((errors <= m_syncThreshold) && ((int) m_collectors.size() < m_maxCollectors))
    {
        Collector collector;
        collector.m_invert = invert;
        collector.m_syncErrors = errors;
        collector.m_needed = m_uspPlsSymbols + (m_uspFrameSize[1] + CCSDS::m_rsParity) * 16;
        collector.m_symbols.reserve(collector.m_needed);
        m_collectors.push_back(collector);
    }
}

bool SSDVUSPDeframer::finishFrame(const Collector& collector, CCSDS::DecodedFrame& frame)
{
    int nbits = (collector.m_needed - m_uspPlsSymbols) / 2;
    int size = nbits / 8;
    int frameSize = size - CCSDS::m_rsParity;
    std::vector<uint8_t> bits(nbits);
    std::vector<uint8_t> codeword(size, 0);

    CCSDS::viterbiDecodeBlock(collector.m_symbols.data() + m_uspPlsSymbols, nbits, bits.data());

    for (int i = 0; i < nbits; i++) {
        codeword[i / 8] |= bits[i] << (7 - (i % 8));
    }

    CCSDS::randomize(codeword.data(), size);
    frame.m_rsErrors = CCSDS::rsDecode(codeword.data(), frameSize, true);
    frame.m_asmErrors = collector.m_syncErrors;
    frame.m_branch = 0;

    if (frame.m_rsErrors < 0)
    {
        // Syncs on noise have several errors; don't count those
        if (collector.m_syncErrors <= 4) {
            m_framesBad++;
        }

        return false;
    }

    int length = codeword[2] | (codeword[3] << 8);

    if ((length > 0) && (length <= frameSize - m_uspHeaderSize)) {
        frame.m_data = QByteArray((const char *) codeword.data() + m_uspHeaderSize, length);
    } else {
        frame.m_data = QByteArray((const char *) codeword.data(), frameSize);
    }

    return true;
}

// GENESIS sync word and the source addresses of AMSAT-EA's satellites
static constexpr uint16_t m_genesisSync = 0xBF35;
static constexpr int m_genesisSources[] = {0x2, 0x3, 0x5, 0xB, 0xC};
static constexpr int m_genesisVoiceSize = 0x25;
static constexpr int m_genesisVoiceTypes[] = {11, 15};
static constexpr int m_genesisPN9Type = 13;

SSDVGenesisDeframer::SSDVGenesisDeframer()
{
    reset();
}

void SSDVGenesisDeframer::reset()
{
    m_sr = 0;
    m_collectors.clear();
    m_framesBad = 0;
}

// AMSAT-EA's self-synchronising descrambler. Bit 0 of each byte is neither descrambled nor shifted in.
void SSDVGenesisDeframer::descramble(uint8_t *data, int length)
{
    uint32_t lfsr = 0x2C350000;

    for (int i = 0; i < length; i++)
    {
        for (int b = 7; b > 0; b--)
        {
            int in = (data[i] >> b) & 1;
            int out = (in ^ (lfsr >> 16) ^ (lfsr >> 11)) & 1;

            if (out) {
                data[i] |= 1 << b;
            } else {
                data[i] &= ~(1 << b);
            }

            lfsr = ((lfsr << 1) | in) & 0x1FFFF;
        }
    }
}

// CRC-16/CCITT-FALSE
uint16_t SSDVGenesisDeframer::crc16(const uint8_t *data, int length)
{
    uint16_t crc = 0xFFFF;

    for (int i = 0; i < length; i++)
    {
        crc ^= data[i] << 8;

        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }

    return crc;
}

void SSDVGenesisDeframer::process(float symbol, QList<CCSDS::DecodedFrame>& frames)
{
    // The lower tone is 1
    int bit = symbol < 0.0f ? 1 : 0;

    for (auto it = m_collectors.begin(); it != m_collectors.end();)
    {
        Collector& collector = *it;
        collector.m_byte = (collector.m_byte << 1) | (bit ^ (collector.m_invert ? 1 : 0));
        bool drop = false;

        if (++collector.m_bits == 8)
        {
            collector.m_bytes.push_back(collector.m_byte);
            collector.m_bits = 0;
            int count = collector.m_bytes.size();

            if (count == 1)
            {
                drop = collector.m_bytes[0] < 3;
            }
            else if (count == 2)
            {
                int type = collector.m_bytes[1] >> 4;
                int source = collector.m_bytes[1] & 0xF;
                drop = (type == 0) || (std::find(std::begin(m_genesisSources), std::end(m_genesisSources), source) == std::end(m_genesisSources));
            }

            if (!drop && (count == 1 + collector.m_bytes[0]))
            {
                CCSDS::DecodedFrame frame;

                if (finishFrame(collector, frame)) {
                    frames.append(frame);
                }

                drop = true;
            }
        }

        if (drop) {
            it = m_collectors.erase(it);
        } else {
            ++it;
        }
    }

    m_sr = (m_sr << 1) | bit;
    bool normal = (m_sr & 0xFFFF) == m_genesisSync;
    bool inverted = (m_sr & 0xFFFF) == (uint16_t) ~m_genesisSync;

    if ((normal || inverted) && ((int) m_collectors.size() < m_maxCollectors))
    {
        Collector collector;
        collector.m_bits = 0;
        collector.m_byte = 0;
        collector.m_invert = inverted;
        // Training before the sync word, so a bad frame is likely a real one rather than noise
        collector.m_training = ((m_sr >> 16) & 0xFF) == (inverted ? 0x55 : 0xAA);
        m_collectors.push_back(collector);
    }
}

bool SSDVGenesisDeframer::finishFrame(const Collector& collector, CCSDS::DecodedFrame& frame)
{
    int size = collector.m_bytes[0];
    const uint8_t *body = collector.m_bytes.data() + 1;
    int type = body[0] >> 4;

    frame.m_rsErrors = 0;
    frame.m_asmErrors = 0;
    frame.m_branch = 0;

    if ((type == m_ssdvType) && (size == m_ssdvSize))
    {
        // SSDV packet, checked by its own CRC-32 and RS
        frame.m_data = QByteArray((const char *) body, size);
        return true;
    }

    // Voice and PN9 test packets have no CRC
    if (((size == m_genesisVoiceSize) && (std::find(std::begin(m_genesisVoiceTypes), std::end(m_genesisVoiceTypes), type) != std::end(m_genesisVoiceTypes)))
        || (type == m_genesisPN9Type)) {
        return false;
    }

    if (crc16(body, size - 2) != ((body[size - 2] << 8) | body[size - 1]))
    {
        if (collector.m_training) {
            m_framesBad++;
        }

        return false;
    }

    std::vector<uint8_t> plain(body, body + size - 2);
    descramble(plain.data() + 1, plain.size() - 1);
    frame.m_data = QByteArray((const char *) plain.data(), plain.size());

    return true;
}
