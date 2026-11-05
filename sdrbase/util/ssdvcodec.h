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

#ifndef INCLUDE_UTIL_SSDVCODEC_H
#define INCLUDE_UTIL_SSDVCODEC_H

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QList>
#include <QMap>
#include <QString>

#include "export.h"

// SSDV (Slow Scan Digital Video) packets: a JPEG image cut into packets that can each be decoded on their own,
// so that a lost packet only blanks part of the image.
// The packet coding itself is Philip Heron's SSDV library (util/ssdv). This wraps it and adds the headerless
// variant used by DSLWP and ASRTU-1 (AO-123).
namespace SSDV {

enum Variant {
    Standard, // 0x55 sync, type, callsign, 15 byte header and 32 bit CRC, optionally followed by 32 RS parity bytes
    DSLWP     // No sync, type or callsign: 9 byte header, 205 bytes of data and a CRC with a different initial value
};

static constexpr int m_standardPacketSize = 256;
static constexpr int m_dslwpPacketSize = 218;

struct SDRBASE_API PacketInfo
{
    Variant m_variant;
    bool m_fec;           // Standard packet with RS parity
    int m_rsErrors;       // Bytes corrected by the packet's own RS code
    QString m_callsign;
    int m_imageId;
    int m_packetId;
    int m_width;
    int m_height;
    int m_quality;
    bool m_eoi;           // Last packet of the image
    int m_mcuMode;
    int m_mcuOffset;
    int m_mcuId;
    int m_mcuCount;

    PacketInfo();
};

// Checks a packet, repairing it with its RS code if it has one, and converts it to a standard packet the decoder
// accepts. The packet must start at the start of data and be exactly variant's size (or, for Standard, between
// 20 and 256 bytes).
// Returns false if the packet is not valid.
SDRBASE_API bool parsePacket(const QByteArray& data, Variant variant, PacketInfo& info, QByteArray& standardPacket);

// Searches data for valid packets of the given variant and packet size, as the SSDV command line decoder does
SDRBASE_API QList<QByteArray> findPackets(const QByteArray& data, Variant variant, int packetSize);

// Builds one image from packets that may arrive out of order, repeated or with gaps
class SDRBASE_API ImageDecoder
{
public:
    ImageDecoder(const QString& callsign, int imageId, int packetSize);

    // Packet as returned by parsePacket. Returns false if it is a repeat
    bool addPacket(const QByteArray& standardPacket, const PacketInfo& info);

    // Decodes the packets received so far. Missing parts of the image are grey.
    QImage image();
    // JPEG built from the packets received so far
    QByteArray jpeg();

    const QString& getCallsign() const { return m_callsign; }
    int getImageId() const { return m_imageId; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    int getPacketsReceived() const { return m_packets.size(); }
    // Total number of packets if the last one has been received, otherwise -1
    int getPacketCount() const { return m_lastPacketId < 0 ? -1 : m_lastPacketId + 1; }
    int getHighestPacketId() const { return m_packets.isEmpty() ? -1 : m_packets.lastKey(); }
    int getMissingPackets() const;
    bool isComplete() const { return getPacketCount() > 0 && getPacketsReceived() >= getPacketCount(); }
    // Fraction of the image's MCUs received, from 0 to 1
    float getProgress() const;
    const QDateTime& getFirstPacketTime() const { return m_firstPacketTime; }
    const QDateTime& getLastPacketTime() const { return m_lastPacketTime; }

private:
    QString m_callsign;
    int m_imageId;
    int m_packetSize;
    int m_width;
    int m_height;
    int m_mcuCount;
    int m_lastPacketId;
    QMap<int, QByteArray> m_packets; // By packet ID
    QMap<int, int> m_packetMcus;     // First MCU in each packet, if any
    QDateTime m_firstPacketTime;
    QDateTime m_lastPacketTime;
};

// Encodes an image into packets
class SDRBASE_API Encoder
{
public:
    // quality is the SSDV quality level 0 to 7 (JPEG quality around 13 to 100). 4 is the SSDV default
    Encoder(Variant variant, const QString& callsign, int imageId, int quality = 4, bool fec = true);

    // Rounds the image's size down to a multiple of 16 (cropping evenly) and limits it to 4080x4080.
    // Packets are standard packets with FEC when the variant is Standard and fec is set, standard packets without
    // FEC when not, or DSLWP packets when the variant is DSLWP.
    // Returns an empty list and sets error if it fails.
    QList<QByteArray> encode(const QImage& image, QString& error);

    // Scales and crops an image to fit width x height, both multiples of 16. Fill pads instead of cropping.
    static QImage fit(const QImage& image, int width, int height, bool fill);

private:
    Variant m_variant;
    QString m_callsign;
    int m_imageId;
    int m_quality;
    bool m_fec;
};

// DSLWP packets carry no callsign; decoded images are labelled with this
extern SDRBASE_API const char * const m_dslwpCallsign;

} // namespace SSDV

#endif // INCLUDE_UTIL_SSDVCODEC_H
