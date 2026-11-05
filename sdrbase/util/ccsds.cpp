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
#include <cstring>

#include "ssdv/rs8.h"
#include "ccsds.h"

namespace CCSDS {

// Generator polynomials with the newest bit in bit 0: 171 and 133 octal reversed
static constexpr uint32_t m_g1 = 0x4F;
static constexpr uint32_t m_g2 = 0x6D;

static inline int parity(uint32_t x)
{
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1;
}

static inline int popcount(uint32_t x)
{
    return (int) std::bitset<32>(x).count();
}

struct Tables
{
    uint8_t m_pn[255];
    uint8_t m_tal[256];   // Conventional to dual basis
    uint8_t m_tal1[256];  // Dual to conventional basis
    uint8_t m_expected[128]; // Viterbi: encoder output for each 7 bit register value

    Tables()
    {
        // Pseudo-randomizer: a(n+8) = a(n+7) ^ a(n+5) ^ a(n+3) ^ a(n), starting with eight ones
        uint8_t a[255 * 8 + 8];
        std::fill(a, a + 8, 1);

        for (int n = 0; n < 255 * 8; n++) {
            a[n + 8] = a[n + 7] ^ a[n + 5] ^ a[n + 3] ^ a[n];
        }

        for (int i = 0; i < 255; i++)
        {
            m_pn[i] = 0;

            for (int j = 0; j < 8; j++) {
                m_pn[i] = (m_pn[i] << 1) | a[i * 8 + j];
            }
        }

        // Dual basis transformation, from CCSDS 131.0-B Annex F, as in Phil Karn's libfec
        static const uint8_t tal[] = { 0x8d, 0xef, 0xec, 0x86, 0xfa, 0x99, 0xaf, 0x7b };

        for (int i = 0; i < 256; i++)
        {
            m_tal[i] = 0;

            for (int j = 0; j < 8; j++)
            {
                for (int k = 0; k < 8; k++)
                {
                    if (i & (1 << k)) {
                        m_tal[i] ^= tal[7 - k] & (1 << j);
                    }
                }
            }

            m_tal1[m_tal[i]] = i;
        }

        for (int sr = 0; sr < 128; sr++) {
            m_expected[sr] = (parity(sr & m_g1) << 1) | (parity(sr & m_g2) ^ 1);
        }
    }
};

static const Tables& tables()
{
    static const Tables t;
    return t;
}

CodingSettings::CodingSettings() :
    m_frameSize(223),
    m_rs(true),
    m_dualBasis(false),
    m_randomizer(true),
    m_differential(true),
    m_convolutional(true)
{
}

bool CodingSettings::operator==(const CodingSettings& other) const
{
    return (m_frameSize == other.m_frameSize)
        && (m_rs == other.m_rs)
        && (m_dualBasis == other.m_dualBasis)
        && (m_randomizer == other.m_randomizer)
        && (m_differential == other.m_differential)
        && (m_convolutional == other.m_convolutional);
}

void randomize(uint8_t *data, int length)
{
    const uint8_t *pn = tables().m_pn;

    for (int i = 0; i < length; i++) {
        data[i] ^= pn[i % 255];
    }
}

void rsEncode(const uint8_t *data, int frameSize, uint8_t *parity, bool dualBasis)
{
    uint8_t d[m_rsMaxData];
    int pad = m_rsMaxData - frameSize;

    if (dualBasis)
    {
        for (int i = 0; i < frameSize; i++) {
            d[i] = tables().m_tal1[data[i]];
        }
    }
    else
    {
        std::copy(data, data + frameSize, d);
    }

    encode_rs_8(d, parity, pad);

    if (dualBasis)
    {
        for (int i = 0; i < m_rsParity; i++) {
            parity[i] = tables().m_tal[parity[i]];
        }
    }
}

int rsDecode(uint8_t *codeword, int frameSize, bool dualBasis)
{
    int pad = m_rsMaxData - frameSize;
    int n = frameSize + m_rsParity;

    if (dualBasis)
    {
        for (int i = 0; i < n; i++) {
            codeword[i] = tables().m_tal1[codeword[i]];
        }
    }

    int errors = decode_rs_8(codeword, nullptr, 0, pad);

    if (dualBasis)
    {
        for (int i = 0; i < n; i++) {
            codeword[i] = tables().m_tal[codeword[i]];
        }
    }

    return errors;
}

int ConvolutionalEncoder::encode(int bit)
{
    m_sr = ((m_sr << 1) | (bit & 1)) & 0x7F;
    return tables().m_expected[m_sr];
}

ViterbiDecoder::ViterbiDecoder()
{
    reset();
}

void ViterbiDecoder::reset()
{
    m_metrics.fill(0.0f);
    m_decisions.fill(0);
    m_index = 0;
    m_count = 0;
}

int ViterbiDecoder::decode(float g1, float g2)
{
    const uint8_t *expected = tables().m_expected;
    std::array<float, 64> metrics;
    uint64_t decisions = 0;
    float best = -1e30f;
    int bestState = 0;

    // State is the last 6 input bits, newest in bit 0. The register for a branch into state ns from the
    // predecessor whose oldest bit was b is ns | (b << 6).
    for (int ns = 0; ns < 64; ns++)
    {
        int e0 = expected[ns];
        int e1 = expected[ns | 64];
        float m0 = m_metrics[ns >> 1] + ((e0 & 2) ? g1 : -g1) + ((e0 & 1) ? g2 : -g2);
        float m1 = m_metrics[(ns >> 1) | 32] + ((e1 & 2) ? g1 : -g1) + ((e1 & 1) ? g2 : -g2);

        if (m1 > m0)
        {
            metrics[ns] = m1;
            decisions |= 1ULL << ns;
        }
        else
        {
            metrics[ns] = m0;
        }

        if (metrics[ns] > best)
        {
            best = metrics[ns];
            bestState = ns;
        }
    }

    for (int ns = 0; ns < 64; ns++) {
        m_metrics[ns] = metrics[ns] - best;
    }

    m_decisions[m_index] = decisions;
    m_index = (m_index + 1) % m_tracebackLength;

    if (m_count < m_tracebackLength)
    {
        m_count++;
        return -1;
    }

    // Trace back from the best state to the oldest step in the buffer
    int state = bestState;
    int bit = 0;

    for (int i = 1; i <= m_tracebackLength; i++)
    {
        int idx = (m_index - i + m_tracebackLength) % m_tracebackLength;
        bit = state & 1;
        int b = (m_decisions[idx] >> state) & 1;
        state = (state >> 1) | (b << 5);
    }

    return bit;
}

FrameEncoder::FrameEncoder() :
    m_diffPrev(0)
{
}

void FrameEncoder::setSettings(const CodingSettings& settings)
{
    m_settings = settings;
}

void FrameEncoder::reset()
{
    m_conv.reset();
    m_diffPrev = 0;
}

void FrameEncoder::encodeBit(int bit, std::vector<uint8_t>& symbols)
{
    if (m_settings.m_differential)
    {
        m_diffPrev ^= bit;
        bit = m_diffPrev;
    }

    if (m_settings.m_convolutional)
    {
        int out = m_conv.encode(bit);
        symbols.push_back((out >> 1) & 1);
        symbols.push_back(out & 1);
    }
    else
    {
        symbols.push_back(bit);
    }
}

void FrameEncoder::encodeBits(const uint8_t *bits, int count, std::vector<uint8_t>& symbols)
{
    for (int i = 0; i < count; i++) {
        encodeBit(bits[i], symbols);
    }
}

void FrameEncoder::encodeFrame(const QByteArray& frame, std::vector<uint8_t>& symbols)
{
    int frameSize = m_settings.m_frameSize;
    std::vector<uint8_t> codeword(m_settings.codewordSize(), 0);
    std::copy(frame.constBegin(), frame.constBegin() + std::min((int) frame.size(), frameSize), codeword.begin());

    if (m_settings.m_rs) {
        rsEncode(codeword.data(), frameSize, &codeword[frameSize], m_settings.m_dualBasis);
    }

    if (m_settings.m_randomizer) {
        randomize(codeword.data(), codeword.size());
    }

    for (int i = m_asmBits - 1; i >= 0; i--) {
        encodeBit((m_asm >> i) & 1, symbols);
    }

    for (uint8_t byte : codeword)
    {
        for (int i = 7; i >= 0; i--) {
            encodeBit((byte >> i) & 1, symbols);
        }
    }
}

FrameDecoder::FrameDecoder() :
    m_asmThreshold(4),
    m_prevSymbol(0.0f),
    m_symbolCount(0),
    m_framesBad(0)
{
    reset();
}

void FrameDecoder::setSettings(const CodingSettings& settings)
{
    if (settings != m_settings)
    {
        m_settings = settings;
        reset();
    }
}

void FrameDecoder::reset()
{
    for (auto& branch : m_branches)
    {
        branch.m_viterbi.reset();
        branch.m_diffPrev = 0;
        branch.m_sr = 0;
        branch.m_bitCount = 0;
        branch.m_collectors.clear();
    }

    m_prevSymbol = 0.0f;
    m_symbolCount = 0;
}

void FrameDecoder::process(float symbol, QList<DecodedFrame>& frames)
{
    if (!m_settings.m_convolutional)
    {
        processBit(0, symbol > 0.0f ? 1 : 0, frames);
        return;
    }

    // Decode both pairings of symbols, as we don't know which symbol is G1
    if (m_symbolCount > 0)
    {
        int branch = m_symbolCount & 1;
        int bit = m_branches[branch].m_viterbi.decode(m_prevSymbol, symbol);

        if (bit >= 0) {
            processBit(branch, bit, frames);
        }
    }

    m_prevSymbol = symbol;
    m_symbolCount++;
}

void FrameDecoder::processBit(int branchIndex, int bit, QList<DecodedFrame>& frames)
{
    Branch& branch = m_branches[branchIndex];

    if (m_settings.m_differential)
    {
        int b = bit ^ branch.m_diffPrev;
        branch.m_diffPrev = bit;
        bit = b;
    }

    // Collect bits for frames whose ASM we've seen
    int codewordBits = m_settings.codewordSize() * 8;

    for (auto it = branch.m_collectors.begin(); it != branch.m_collectors.end();)
    {
        it->m_bits.push_back(bit ^ (it->m_invert ? 1 : 0));

        if ((int) it->m_bits.size() == codewordBits)
        {
            DecodedFrame frame;

            if (finishFrame(*it, branchIndex, frame)) {
                frames.append(frame);
            }

            it = branch.m_collectors.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Search for the ASM. Without differential coding, inverted bits are possible.
    branch.m_sr = (branch.m_sr << 1) | bit;
    branch.m_bitCount++;

    if (branch.m_bitCount >= m_asmBits)
    {
        int errors = popcount(branch.m_sr ^ m_asm);
        int invErrors = m_settings.m_differential ? m_asmBits : popcount(~branch.m_sr ^ m_asm);

        if ((errors <= m_asmThreshold) || (invErrors <= m_asmThreshold))
        {
            Collector collector;
            collector.m_invert = invErrors < errors;
            collector.m_asmErrors = std::min(errors, invErrors);
            collector.m_bits.reserve(codewordBits);
            branch.m_collectors.push_back(collector);
        }
    }
}

bool FrameDecoder::finishFrame(Collector& collector, int branchIndex, DecodedFrame& frame)
{
    int size = m_settings.codewordSize();
    std::vector<uint8_t> codeword(size, 0);

    for (int i = 0; i < size; i++)
    {
        for (int j = 0; j < 8; j++) {
            codeword[i] = (codeword[i] << 1) | collector.m_bits[i * 8 + j];
        }
    }

    if (m_settings.m_randomizer) {
        randomize(codeword.data(), size);
    }

    frame.m_rsErrors = 0;

    if (m_settings.m_rs)
    {
        frame.m_rsErrors = rsDecode(codeword.data(), m_settings.m_frameSize, m_settings.m_dualBasis);

        if (frame.m_rsErrors < 0)
        {
            // Most syncs on noise have several ASM errors; don't count those
            if (collector.m_asmErrors <= 1) {
                m_framesBad++;
            }

            return false;
        }
    }

    frame.m_data = QByteArray((const char *) codeword.data(), m_settings.m_frameSize);
    frame.m_asmErrors = collector.m_asmErrors;
    frame.m_branch = branchIndex;

    return true;
}

TMShortHeader::TMShortHeader() :
    m_version(0),
    m_spacecraftId(0),
    m_virtualChannel(0),
    m_ocf(false),
    m_masterChannelCount(0),
    m_virtualChannelCount(0),
    m_firstHeaderPointer(0)
{
}

bool TMShortHeader::decode(const QByteArray& frame)
{
    if (frame.size() < m_size) {
        return false;
    }

    const uint8_t *p = (const uint8_t *) frame.constData();
    m_version = p[0] >> 6;
    m_spacecraftId = ((p[0] & 0x3F) << 4) | (p[1] >> 4);
    m_virtualChannel = (p[1] >> 1) & 7;
    m_ocf = p[1] & 1;
    m_masterChannelCount = p[2];
    m_virtualChannelCount = p[3];
    m_firstHeaderPointer = p[4];

    return true;
}

void TMShortHeader::encode(uint8_t *p) const
{
    p[0] = ((m_version & 3) << 6) | ((m_spacecraftId >> 4) & 0x3F);
    p[1] = ((m_spacecraftId & 0xF) << 4) | ((m_virtualChannel & 7) << 1) | (m_ocf ? 1 : 0);
    p[2] = m_masterChannelCount;
    p[3] = m_virtualChannelCount;
    p[4] = m_firstHeaderPointer;
}

} // namespace CCSDS
