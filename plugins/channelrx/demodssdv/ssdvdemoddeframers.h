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

#ifndef INCLUDE_SSDVDEMODDEFRAMERS_H
#define INCLUDE_SSDVDEMODDEFRAMERS_H

#include <vector>
#include <cstdint>

#include <QList>

#include "util/ccsds.h"

// Unified SPUTNIX Protocol (USP), as sent by SPUTNIX satellites such as Luca (RS90S). After the description in
// gr-satellites (usp_deframer): a 64 bit sync word, a 64 symbol DVB-S2 style PLS code that gives the frame
// length (48 or 223 bytes), then the CCSDS rate 1/2, K=7 convolutional code (not terminated), CCSDS randomizer and
// RS(255,223) in dual basis. The frame holds a 4 byte header, whose bytes 2-3 are the length (little endian) of
// the AX.25 frame that follows. Frames are output as that AX.25 frame (without CRC).
class SSDVUSPDeframer
{
public:
    SSDVUSPDeframer();
    void reset();
    void process(float symbol, QList<CCSDS::DecodedFrame>& frames);
    int getFramesBad() const { return m_framesBad; }

    static constexpr int m_syncThreshold = 10; // Bit errors allowed in the sync word

private:
    struct Collector
    {
        std::vector<float> m_symbols;
        bool m_invert;
        int m_syncErrors;
        int m_needed;      // Symbols after the sync word, known once the PLS code has been received
    };

    uint64_t m_sr;
    int m_count;
    std::vector<Collector> m_collectors;
    int m_framesBad;

    bool finishFrame(const Collector& collector, CCSDS::DecodedFrame& frame);
};

// AMSAT-EA's GENESIS framing, as sent by HADES-SA (SO-127), HADES-L etc. After AMSAT-EA's documents and decoder
// and the HADESx decoder: 2-FSK with the lower tone for 1, 0xAA training, 0xBF35 sync word, a size byte (bytes
// that follow it), a type / source address byte, data scrambled with x^17+x^12+1, and a CRC-16/CCITT-FALSE over
// the type byte and the scrambled data.
// SSDV packets (type 10) and some others are sent without scrambling or CRC-16; SSDV packets are checked by their
// own CRC-32 later. Frames are output from the type byte on, with the data descrambled and the CRC removed, as
// AMSAT-EA's decoder takes them; SSDV packets as sent.
class SSDVGenesisDeframer
{
public:
    SSDVGenesisDeframer();
    void reset();
    // Symbol positive for the higher tone
    void process(float symbol, QList<CCSDS::DecodedFrame>& frames);
    int getFramesBad() const { return m_framesBad; }

    static constexpr int m_ssdvType = 10;
    static constexpr int m_ssdvSize = 251; // Size byte of SSDV packets

    static void descramble(uint8_t *data, int length);
    static uint16_t crc16(const uint8_t *data, int length);

private:
    struct Collector
    {
        std::vector<uint8_t> m_bytes; // From the size byte
        int m_bits;
        uint8_t m_byte;
        bool m_invert;
        bool m_training;
    };

    uint32_t m_sr;
    std::vector<Collector> m_collectors;
    int m_framesBad;

    // Returns false if the frame isn't one to report
    bool finishFrame(const Collector& collector, CCSDS::DecodedFrame& frame);
};

#endif // INCLUDE_SSDVDEMODDEFRAMERS_H
