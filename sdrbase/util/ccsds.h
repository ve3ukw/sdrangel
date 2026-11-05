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

#ifndef INCLUDE_UTIL_CCSDS_H
#define INCLUDE_UTIL_CCSDS_H

#include <QByteArray>
#include <QList>

#include <array>
#include <vector>
#include <cstdint>

#include "export.h"

// CCSDS TM synchronization and channel coding (CCSDS 131.0-B): attached sync marker, pseudo-randomizer,
// Reed-Solomon (255,223) and the rate 1/2, K=7 convolutional code, as used by many amateur satellites
// (in gr-satellites terms, "CCSDS Concatenated" and "CCSDS Reed-Solomon" framing).
namespace CCSDS {

static constexpr uint32_t m_asm = 0x1ACFFC1D;
static constexpr int m_asmBits = 32;
static constexpr int m_rsParity = 32;
static constexpr int m_rsMaxData = 223;

struct SDRBASE_API CodingSettings
{
    int m_frameSize;        // Bytes of frame data, without RS parity. Up to 223 if m_rs (shortened code if fewer)
    bool m_rs;              // Reed-Solomon (255,223), conventional basis, interleaving depth 1
    bool m_dualBasis;       // Reed-Solomon in CCSDS dual basis rather than conventional
    bool m_randomizer;      // CCSDS pseudo-randomizer
    bool m_differential;    // Differential (NRZ-M) coding of the bits, inside the convolutional code
    bool m_convolutional;   // Rate 1/2, K=7 convolutional code (CCSDS: 171, inverted 133)

    CodingSettings();
    // Bytes after the ASM: frame plus parity
    int codewordSize() const { return m_frameSize + (m_rs ? m_rsParity : 0); }
    bool operator==(const CodingSettings& other) const;
    bool operator!=(const CodingSettings& other) const { return !(*this == other); }
};

// XOR with the CCSDS pseudo-random sequence (x^8+x^7+x^5+x^3+1, starting with all ones)
SDRBASE_API void randomize(uint8_t *data, int length);

// RS(255,223) with CCSDS field and generator, shortened to frameSize bytes of data.
SDRBASE_API void rsEncode(const uint8_t *data, int frameSize, uint8_t *parity, bool dualBasis = false);
// Corrects codeword (frameSize bytes followed by 32 parity bytes) in place.
// Returns the number of bytes corrected, or -1 if it can't be corrected.
SDRBASE_API int rsDecode(uint8_t *codeword, int frameSize, bool dualBasis = false);

// K=7, rate 1/2 convolutional encoder. Two output bits for each input bit, G1 first, G2 inverted.
class SDRBASE_API ConvolutionalEncoder
{
public:
    ConvolutionalEncoder() : m_sr(0) {}
    void reset() { m_sr = 0; }
    // Returns G1 output in bit 1 and G2 output in bit 0
    int encode(int bit);
private:
    uint32_t m_sr;
};

// Soft decision Viterbi decoder for the code above. Decodes a continuous stream.
class SDRBASE_API ViterbiDecoder
{
public:
    ViterbiDecoder();
    void reset();
    // Soft symbols for one input bit: positive means 1, the larger the more certain.
    // Returns the decoded bit from m_tracebackLength bits ago, or -1 while the decoder is filling.
    int decode(float g1, float g2);

    static constexpr int m_tracebackLength = 96;

private:
    std::array<float, 64> m_metrics;
    std::array<uint64_t, m_tracebackLength> m_decisions; // One bit per state, ring buffer
    int m_index;
    int m_count;
};

// Turns frames into the bits to transmit: ASM, frame, RS parity, then randomized, differentially coded and
// convolutionally coded as configured. Symbols are 0 or 1, two per data bit with the convolutional code.
class SDRBASE_API FrameEncoder
{
public:
    FrameEncoder();
    void setSettings(const CodingSettings& settings);
    const CodingSettings& getSettings() const { return m_settings; }
    void reset();
    // Frame must be m_frameSize bytes (it is padded with zeros or truncated)
    void encodeFrame(const QByteArray& frame, std::vector<uint8_t>& symbols);
    // Bits with no frame (e.g. to flush the decoders at the end of a transmission)
    void encodeBits(const uint8_t *bits, int count, std::vector<uint8_t>& symbols);

private:
    void encodeBit(int bit, std::vector<uint8_t>& symbols);

    CodingSettings m_settings;
    ConvolutionalEncoder m_conv;
    int m_diffPrev;
};

struct SDRBASE_API DecodedFrame
{
    QByteArray m_data;      // m_frameSize bytes
    int m_rsErrors;         // Bytes corrected by RS, 0 if no RS
    int m_asmErrors;        // Bit errors in the ASM
    int m_branch;           // Which symbol pairing the Viterbi decoder used (0 or 1)
};

// Recovers frames from soft symbols (positive means 1), searching for the ASM in the decoded bit stream.
// Symbols may be inverted (180 degree BPSK phase ambiguity) when the differential coding is on, or when the
// convolutional code is used (it is transparent: inverted symbols decode to inverted bits, which the ASM search
// accepts when differential coding is off).
class SDRBASE_API FrameDecoder
{
public:
    FrameDecoder();
    void setSettings(const CodingSettings& settings);
    const CodingSettings& getSettings() const { return m_settings; }
    void setAsmThreshold(int threshold) { m_asmThreshold = threshold; }
    void reset();
    // Returns frames that ended with this symbol
    void process(float symbol, QList<DecodedFrame>& frames);

    int getFramesBad() const { return m_framesBad; } // Frames whose RS decoding failed, with at most 1 ASM error

private:
    struct Collector
    {
        std::vector<uint8_t> m_bits;
        bool m_invert;
        int m_asmErrors;
    };

    struct Branch
    {
        ViterbiDecoder m_viterbi;
        int m_diffPrev;
        uint32_t m_sr;
        int m_bitCount;
        std::vector<Collector> m_collectors;
    };

    void processBit(int branchIndex, int bit, QList<DecodedFrame>& frames);
    bool finishFrame(Collector& collector, int branchIndex, DecodedFrame& frame);

    CodingSettings m_settings;
    int m_asmThreshold;
    Branch m_branches[2];
    float m_prevSymbol;
    int m_symbolCount;
    int m_framesBad;
};

// CCSDS TM transfer frame primary header in the 5 byte short form used by DSLWP, BY70, ASRTU-1 etc.
// (version 2 bits, spacecraft ID 10, virtual channel 3, OCF flag 1, master channel count 8,
// virtual channel count 8, first header pointer 8)
struct SDRBASE_API TMShortHeader
{
    static constexpr int m_size = 5;

    int m_version;
    int m_spacecraftId;
    int m_virtualChannel;
    bool m_ocf;
    int m_masterChannelCount;
    int m_virtualChannelCount;
    int m_firstHeaderPointer;

    TMShortHeader();
    bool decode(const QByteArray& frame);
    void encode(uint8_t *p) const;
};

} // namespace CCSDS

#endif // INCLUDE_UTIL_CCSDS_H
