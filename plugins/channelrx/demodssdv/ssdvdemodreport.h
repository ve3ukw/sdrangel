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

#ifndef INCLUDE_SSDVDEMODREPORT_H
#define INCLUDE_SSDVDEMODREPORT_H

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QString>

#include "util/message.h"
#include "util/ssdvcodec.h"

// Messages from the sink (baseband thread) to the channel, and from the channel to the GUI
class SSDVDemodReport
{
public:
    //! A frame that passed the CCSDS decoding
    class MsgFrame : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        const QByteArray& getData() const { return m_data; }
        int getRSErrors() const { return m_rsErrors; }
        int getASMErrors() const { return m_asmErrors; }
        const QDateTime& getDateTime() const { return m_dateTime; }

        static MsgFrame *create(const QByteArray& data, int rsErrors, int asmErrors, const QDateTime& dateTime) {
            return new MsgFrame(data, rsErrors, asmErrors, dateTime);
        }

    private:
        QByteArray m_data;
        int m_rsErrors;
        int m_asmErrors;
        QDateTime m_dateTime;

        MsgFrame(const QByteArray& data, int rsErrors, int asmErrors, const QDateTime& dateTime) :
            Message(),
            m_data(data),
            m_rsErrors(rsErrors),
            m_asmErrors(asmErrors),
            m_dateTime(dateTime)
        {}
    };

    //! An SSDV packet found in the frames (channel to GUI)
    class MsgPacket : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        const SSDV::PacketInfo& getInfo() const { return m_info; }
        const QDateTime& getDateTime() const { return m_dateTime; }
        bool getRepeat() const { return m_repeat; }

        static MsgPacket *create(const SSDV::PacketInfo& info, const QDateTime& dateTime, bool repeat) {
            return new MsgPacket(info, dateTime, repeat);
        }

    private:
        SSDV::PacketInfo m_info;
        QDateTime m_dateTime;
        bool m_repeat;

        MsgPacket(const SSDV::PacketInfo& info, const QDateTime& dateTime, bool repeat) :
            Message(),
            m_info(info),
            m_dateTime(dateTime),
            m_repeat(repeat)
        {}
    };

    //! The image a packet belongs to, decoded from the packets received so far (channel to GUI)
    class MsgImage : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        const QImage& getImage() const { return m_image; }
        const QString& getCallsign() const { return m_callsign; }
        int getImageId() const { return m_imageId; }
        int getPacketsReceived() const { return m_packetsReceived; }
        int getPacketCount() const { return m_packetCount; } //!< -1 until the last packet is received
        int getMissingPackets() const { return m_missingPackets; }
        float getProgress() const { return m_progress; }
        bool getComplete() const { return m_complete; }
        const QString& getSavedFile() const { return m_savedFile; }

        static MsgImage *create(const QImage& image, const QString& callsign, int imageId, int packetsReceived,
            int packetCount, int missingPackets, float progress, bool complete, const QString& savedFile)
        {
            return new MsgImage(image, callsign, imageId, packetsReceived, packetCount, missingPackets, progress,
                complete, savedFile);
        }

    private:
        QImage m_image;
        QString m_callsign;
        int m_imageId;
        int m_packetsReceived;
        int m_packetCount;
        int m_missingPackets;
        float m_progress;
        bool m_complete;
        QString m_savedFile;

        MsgImage(const QImage& image, const QString& callsign, int imageId, int packetsReceived, int packetCount,
            int missingPackets, float progress, bool complete, const QString& savedFile) :
            Message(),
            m_image(image),
            m_callsign(callsign),
            m_imageId(imageId),
            m_packetsReceived(packetsReceived),
            m_packetCount(packetCount),
            m_missingPackets(missingPackets),
            m_progress(progress),
            m_complete(complete),
            m_savedFile(savedFile)
        {}
    };
};

#endif // INCLUDE_SSDVDEMODREPORT_H
