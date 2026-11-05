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

#ifndef INCLUDE_SSDVMODREPORT_H
#define INCLUDE_SSDVMODREPORT_H

#include <vector>

#include <QImage>
#include <QString>

#include "util/message.h"

// Messages between the channel, the source (baseband thread) and the GUI
class SSDVModReport
{
public:
    //! Symbols of a whole transmission (channel to source)
    class MsgTransmit : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        const std::vector<uint8_t>& getSymbols() const { return m_symbols; }
        //! For each symbol index where a frame ends, the packets complete once it has been sent
        const std::vector<std::pair<int, int>>& getProgress() const { return m_progress; }

        static MsgTransmit *create(const std::vector<uint8_t>& symbols, const std::vector<std::pair<int, int>>& progress) {
            return new MsgTransmit(symbols, progress);
        }

    private:
        std::vector<uint8_t> m_symbols;
        std::vector<std::pair<int, int>> m_progress;

        MsgTransmit(const std::vector<uint8_t>& symbols, const std::vector<std::pair<int, int>>& progress) :
            Message(),
            m_symbols(symbols),
            m_progress(progress)
        {}
    };

    //! Stop transmitting (channel to source)
    class MsgAbort : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        static MsgAbort *create() {
            return new MsgAbort();
        }

    private:
        MsgAbort() :
            Message()
        {}
    };

    //! Packets sent (source to channel, and channel to GUI)
    class MsgProgress : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        int getPacketsSent() const { return m_packetsSent; }
        int getPacketCount() const { return m_packetCount; }
        bool getDone() const { return m_done; }

        static MsgProgress *create(int packetsSent, int packetCount, bool done) {
            return new MsgProgress(packetsSent, packetCount, done);
        }

    private:
        int m_packetsSent;
        int m_packetCount;
        bool m_done;

        MsgProgress(int packetsSent, int packetCount, bool done) :
            Message(),
            m_packetsSent(packetsSent),
            m_packetCount(packetCount),
            m_done(done)
        {}
    };

    //! The image being sent, as scaled, or why it can't be (channel to GUI)
    class MsgImage : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        const QImage& getImage() const { return m_image; }
        const QString& getFilename() const { return m_filename; }
        int getImageId() const { return m_imageId; }
        int getPacketCount() const { return m_packetCount; }
        const QString& getError() const { return m_error; }

        static MsgImage *create(const QImage& image, const QString& filename, int imageId, int packetCount, const QString& error) {
            return new MsgImage(image, filename, imageId, packetCount, error);
        }

    private:
        QImage m_image;
        QString m_filename;
        int m_imageId;
        int m_packetCount;
        QString m_error;

        MsgImage(const QImage& image, const QString& filename, int imageId, int packetCount, const QString& error) :
            Message(),
            m_image(image),
            m_filename(filename),
            m_imageId(imageId),
            m_packetCount(packetCount),
            m_error(error)
        {}
    };
};

#endif // INCLUDE_SSDVMODREPORT_H
