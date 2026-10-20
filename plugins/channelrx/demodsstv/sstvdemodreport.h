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

#ifndef INCLUDE_SSTVDEMODREPORT_H
#define INCLUDE_SSTVDEMODREPORT_H

#include <QImage>

#include "util/message.h"

// Messages from the decoder (baseband thread) to the channel, which forwards them to the GUI
class SSTVDemodReport
{
public:
    class MsgImageStarted : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        int getModeIndex() const { return m_modeIndex; }
        int getVisCode() const { return m_visCode; } //!< -1 when started from sync pulses

        static MsgImageStarted *create(int modeIndex, int visCode) {
            return new MsgImageStarted(modeIndex, visCode);
        }

    private:
        int m_modeIndex;
        int m_visCode;

        MsgImageStarted(int modeIndex, int visCode) :
            Message(),
            m_modeIndex(modeIndex),
            m_visCode(visCode)
        {}
    };

    class MsgLines : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        int getFirstRow() const { return m_firstRow; }
        const QImage& getRows() const { return m_rows; }
        int getRowsDone() const { return m_rowsDone; }
        double getClockErrorPpm() const { return m_clockErrorPpm; }

        static MsgLines *create(int firstRow, const QImage& rows, int rowsDone, double clockErrorPpm) {
            return new MsgLines(firstRow, rows, rowsDone, clockErrorPpm);
        }

    private:
        int m_firstRow;
        QImage m_rows;
        int m_rowsDone;
        double m_clockErrorPpm;

        MsgLines(int firstRow, const QImage& rows, int rowsDone, double clockErrorPpm) :
            Message(),
            m_firstRow(firstRow),
            m_rows(rows),
            m_rowsDone(rowsDone),
            m_clockErrorPpm(clockErrorPpm)
        {}
    };

    class MsgImageFinished : public Message
    {
        MESSAGE_CLASS_DECLARATION

    public:
        const QImage& getImage() const { return m_image; }
        bool getComplete() const { return m_complete; }

        static MsgImageFinished *create(const QImage& image, bool complete) {
            return new MsgImageFinished(image, complete);
        }

    private:
        QImage m_image;
        bool m_complete;

        MsgImageFinished(const QImage& image, bool complete) :
            Message(),
            m_image(image),
            m_complete(complete)
        {}
    };
};

#endif // INCLUDE_SSTVDEMODREPORT_H
