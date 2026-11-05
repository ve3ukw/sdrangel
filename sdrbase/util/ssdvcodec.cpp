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

#include <QBuffer>
#include <QPainter>

#include <vector>

#include "ssdv/ssdv.h"
#include "ssdvcodec.h"

namespace SSDV {

const char * const m_dslwpCallsign = "DSLWP";

// Sizes as in ssdv.h
static constexpr int m_headerSize = SSDV_PKT_SIZE_HEADER;      // Standard header, including the sync byte
static constexpr int m_dslwpHeaderSize = 9;                    // Image ID to MCU ID
static constexpr int m_crcSize = SSDV_PKT_SIZE_CRC;
static constexpr int m_rsSize = SSDV_PKT_SIZE_RSCODES;
static constexpr int m_dslwpPayloadSize = m_dslwpPacketSize - m_dslwpHeaderSize - m_crcSize;
// Standard no-FEC packet holding the same data as a DSLWP packet
static constexpr int m_dslwpStandardSize = m_headerSize + m_dslwpPayloadSize + m_crcSize;
// DSLWP CRC-32 initial value (gr-satellites' SSDV fork)
static constexpr quint32 m_dslwpCrcInit = 0x4EE4FDE1;
static constexpr int m_minStandardSize = m_headerSize + m_crcSize + 2;

static quint32 crc32(const quint8 *data, int length, quint32 crc = 0xFFFFFFFF)
{
    for (int i = 0; i < length; i++)
    {
        quint32 x = (crc ^ data[i]) & 0xFF;

        for (int j = 0; j < 8; j++) {
            x = (x & 1) ? (x >> 1) ^ 0xEDB88320 : x >> 1;
        }

        crc = (crc >> 8) ^ x;
    }

    return crc ^ 0xFFFFFFFF;
}

static quint32 getU32(const quint8 *p)
{
    return ((quint32) p[0] << 24) | ((quint32) p[1] << 16) | ((quint32) p[2] << 8) | p[3];
}

static void putU32(quint8 *p, quint32 x)
{
    p[0] = x >> 24;
    p[1] = x >> 16;
    p[2] = x >> 8;
    p[3] = x;
}

// Base 40 callsign, as encode_callsign in ssdv.c
static quint32 encodeCallsign(const QString& callsign)
{
    QByteArray c = callsign.toLatin1().left(SSDV_MAX_CALLSIGN);
    quint32 x = 0;

    for (int i = c.size() - 1; i >= 0; i--)
    {
        char ch = c[i];
        x *= 40;

        if ((ch >= 'A') && (ch <= 'Z')) {
            x += ch - 'A' + 14;
        } else if ((ch >= 'a') && (ch <= 'z')) {
            x += ch - 'a' + 14;
        } else if ((ch >= '0') && (ch <= '9')) {
            x += ch - '0' + 1;
        }
    }

    return x;
}

PacketInfo::PacketInfo() :
    m_variant(Standard),
    m_fec(false),
    m_rsErrors(0),
    m_imageId(0),
    m_packetId(0),
    m_width(0),
    m_height(0),
    m_quality(0),
    m_eoi(false),
    m_mcuMode(0),
    m_mcuOffset(0),
    m_mcuId(0),
    m_mcuCount(0)
{
}

static void getInfo(quint8 *standardPacket, PacketInfo& info)
{
    ssdv_packet_info_t p;

    ssdv_dec_header(&p, standardPacket);
    info.m_fec = p.type == SSDV_TYPE_NORMAL;
    info.m_callsign = QString(p.callsign_s);
    info.m_imageId = p.image_id;
    info.m_packetId = p.packet_id;
    info.m_width = p.width;
    info.m_height = p.height;
    info.m_quality = p.quality;
    info.m_eoi = p.eoi;
    info.m_mcuMode = p.mcu_mode;
    info.m_mcuOffset = p.mcu_offset;
    info.m_mcuId = p.mcu_id;
    info.m_mcuCount = p.mcu_count;
}

bool parsePacket(const QByteArray& data, Variant variant, PacketInfo& info, QByteArray& standardPacket)
{
    quint8 pkt[SSDV_PKT_SIZE];

    info.m_variant = variant;
    info.m_rsErrors = 0;

    if (variant == DSLWP)
    {
        if (data.size() != m_dslwpPacketSize) {
            return false;
        }

        const quint8 *d = (const quint8 *) data.constData();
        int crcData = m_dslwpHeaderSize + m_dslwpPayloadSize;

        if (crc32(d, crcData, m_dslwpCrcInit) != getU32(&d[crcData])) {
            return false;
        }

        // Rebuild it as a standard no-FEC packet
        pkt[0] = 0x55;
        pkt[1] = 0x66 + SSDV_TYPE_NOFEC;
        putU32(&pkt[2], encodeCallsign(m_dslwpCallsign));
        memcpy(&pkt[6], d, crcData);
        putU32(&pkt[6 + crcData], crc32(&pkt[1], m_dslwpStandardSize - 1 - m_crcSize));

        if (ssdv_dec_is_packet(pkt, m_dslwpStandardSize, nullptr) != 0) {
            return false;
        }

        standardPacket = QByteArray((const char *) pkt, m_dslwpStandardSize);
    }
    else
    {
        if ((data.size() < m_minStandardSize) || (data.size() > SSDV_PKT_SIZE)) {
            return false;
        }

        memcpy(pkt, data.constData(), data.size());

        if (ssdv_dec_is_packet(pkt, data.size(), &info.m_rsErrors) != 0) {
            return false;
        }

        standardPacket = QByteArray((const char *) pkt, data.size());
    }

    getInfo(pkt, info);

    return true;
}

QList<QByteArray> findPackets(const QByteArray& data, Variant variant, int packetSize)
{
    QList<QByteArray> packets;
    int i = 0;

    while (i + packetSize <= data.size())
    {
        PacketInfo info;
        QByteArray standardPacket;

        if (((variant == DSLWP) || ((quint8) data[i] == 0x55))
            && parsePacket(data.mid(i, packetSize), variant, info, standardPacket))
        {
            packets.append(data.mid(i, packetSize));
            i += packetSize;
        }
        else
        {
            i++;
        }
    }

    return packets;
}

ImageDecoder::ImageDecoder(const QString& callsign, int imageId, int packetSize) :
    m_callsign(callsign),
    m_imageId(imageId),
    m_packetSize(packetSize),
    m_width(0),
    m_height(0),
    m_mcuCount(0),
    m_lastPacketId(-1)
{
}

bool ImageDecoder::addPacket(const QByteArray& standardPacket, const PacketInfo& info)
{
    if (m_packets.contains(info.m_packetId) || (standardPacket.size() != m_packetSize)) {
        return false;
    }

    QDateTime now = QDateTime::currentDateTime();

    if (m_packets.isEmpty()) {
        m_firstPacketTime = now;
    }

    m_lastPacketTime = now;
    m_width = info.m_width;
    m_height = info.m_height;
    m_mcuCount = info.m_mcuCount;
    m_packets.insert(info.m_packetId, standardPacket);

    if (info.m_mcuId != 0xFFFF) {
        m_packetMcus.insert(info.m_packetId, info.m_mcuId);
    }

    if (info.m_eoi) {
        m_lastPacketId = info.m_packetId;
    }

    return true;
}

int ImageDecoder::getMissingPackets() const
{
    int count = getPacketCount() > 0 ? getPacketCount() : getHighestPacketId() + 1;

    return std::max(0, count - (int) m_packets.size());
}

float ImageDecoder::getProgress() const
{
    if (isComplete()) {
        return 1.0f;
    }

    if ((m_mcuCount <= 0) || m_packets.isEmpty()) {
        return 0.0f;
    }

    // Estimate from the MCUs that start in received packets, assuming roughly equal sized packets
    int received = 0;
    int highestMcu = 0;

    for (auto it = m_packetMcus.cbegin(); it != m_packetMcus.cend(); ++it) {
        highestMcu = std::max(highestMcu, it.value());
    }

    int highestPacket = getHighestPacketId();
    received = m_packets.size();
    float mcusPerPacket = highestPacket > 0 ? highestMcu / (float) highestPacket : 0.0f;

    if (mcusPerPacket <= 0.0f) {
        return 0.0f;
    }

    return std::min(1.0f, received * mcusPerPacket / m_mcuCount);
}

QByteArray ImageDecoder::jpeg()
{
    ssdv_t ssdv;

    if (m_packets.isEmpty() || (ssdv_dec_init(&ssdv, m_packetSize) != SSDV_OK)) {
        return QByteArray();
    }

    // Generous: SSDV output is not much bigger than the packet data, plus tables
    std::vector<quint8> buffer(std::max((int) m_packets.size() * m_packetSize * 4, 1024 * 1024));
    ssdv_dec_set_buffer(&ssdv, buffer.data(), buffer.size());

    for (auto it = m_packets.cbegin(); it != m_packets.cend(); ++it)
    {
        QByteArray packet = it.value();
        ssdv_dec_feed(&ssdv, (quint8 *) packet.data());
    }

    quint8 *jpeg;
    size_t length;
    ssdv_dec_get_jpeg(&ssdv, &jpeg, &length);

    return QByteArray((const char *) jpeg, length);
}

QImage ImageDecoder::image()
{
    QImage image;
    QByteArray data = jpeg();

    if (!data.isEmpty()) {
        image.loadFromData(data, "JPEG");
    }

    return image;
}

Encoder::Encoder(Variant variant, const QString& callsign, int imageId, int quality, bool fec) :
    m_variant(variant),
    m_callsign(callsign),
    m_imageId(imageId),
    m_quality(quality),
    m_fec(fec)
{
}

QImage Encoder::fit(const QImage& image, int width, int height, bool fill)
{
    if (image.isNull()) {
        return image;
    }

    QImage scaled = image.scaled(width, height,
        fill ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation);
    QImage out(width, height, QImage::Format_RGB888);
    out.fill(Qt::black);
    QPainter painter(&out);
    painter.drawImage((width - scaled.width()) / 2, (height - scaled.height()) / 2, scaled);
    painter.end();

    return out;
}

QList<QByteArray> Encoder::encode(const QImage& image, QString& error)
{
    QList<QByteArray> packets;

    if (image.isNull())
    {
        error = "No image";
        return packets;
    }

    // SSDV needs a baseline JPEG whose size is a multiple of 16
    int width = std::min(image.width(), 4080) & ~15;
    int height = std::min(image.height(), 4080) & ~15;

    if ((width == 0) || (height == 0))
    {
        error = "Image is smaller than 16x16";
        return packets;
    }

    QImage cropped = image.copy((image.width() - width) / 2, (image.height() - height) / 2, width, height);

    if (!cropped.isGrayscale()) {
        cropped = cropped.convertToFormat(QImage::Format_RGB888);
    }

    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);

    if (!cropped.save(&buffer, "JPEG", 95))
    {
        error = "JPEG encoding failed";
        return packets;
    }

    buffer.close();

    int type;
    int packetSize;

    if (m_variant == DSLWP)
    {
        type = SSDV_TYPE_NOFEC;
        packetSize = m_dslwpStandardSize;
    }
    else
    {
        type = m_fec ? SSDV_TYPE_NORMAL : SSDV_TYPE_NOFEC;
        packetSize = m_standardPacketSize;
    }

    ssdv_t ssdv;
    QByteArray callsign = (m_variant == DSLWP ? QString(m_dslwpCallsign) : m_callsign).toLatin1();

    if (ssdv_enc_init(&ssdv, type, callsign.data(), m_imageId, m_quality, packetSize) != SSDV_OK)
    {
        error = "SSDV encoder initialisation failed";
        return packets;
    }

    quint8 pkt[SSDV_PKT_SIZE];
    ssdv_enc_set_buffer(&ssdv, pkt);
    bool fed = false;

    while (true)
    {
        char c;

        while ((c = ssdv_enc_get_packet(&ssdv)) == SSDV_FEED_ME)
        {
            if (fed) {
                break;
            }

            ssdv_enc_feed(&ssdv, (quint8 *) jpeg.data(), jpeg.size());
            fed = true;
        }

        if (c == SSDV_EOI)
        {
            break;
        }
        else if (c != SSDV_OK)
        {
            error = c == SSDV_FEED_ME ? "Truncated JPEG" : "SSDV encoding failed";
            packets.clear();
            return packets;
        }

        if (m_variant == DSLWP)
        {
            // Strip the sync, type and callsign, and use the DSLWP CRC
            QByteArray packet((const char *) &pkt[6], m_dslwpPacketSize);
            quint8 *d = (quint8 *) packet.data();
            int crcData = m_dslwpHeaderSize + m_dslwpPayloadSize;
            putU32(&d[crcData], crc32(d, crcData, m_dslwpCrcInit));
            packets.append(packet);
        }
        else
        {
            packets.append(QByteArray((const char *) pkt, packetSize));
        }
    }

    if (packets.isEmpty()) {
        error = "SSDV encoder produced no packets";
    }

    return packets;
}

} // namespace SSDV
