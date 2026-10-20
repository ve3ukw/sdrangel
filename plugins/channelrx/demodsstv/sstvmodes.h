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

#ifndef INCLUDE_SSTVMODES_H
#define INCLUDE_SSTVMODES_H

#include <QString>
#include <QStringList>

// Timing of the supported SSTV modes, all in one table.
//
// A mode is described as a sequence of sync periods. Each period starts with a
// sync pulse (1200 Hz) and carries one image line (two for PD modes). The video
// segments are given as offsets from the start of the sync pulse, so modes such
// as Scottie, where the sync is in the middle of the line, have negative offsets.
struct SSTVMode
{
    enum Id
    {
        Robot36,
        Robot72,
        PD50,
        PD90,
        PD120,
        PD160,
        PD180,
        PD240,
        PD290,
        Martin1,
        Martin2,
        Scottie1,
        Scottie2,
        ScottieDX,
        NbModes
    };

    enum ColorFormat
    {
        RGB,        //!< Separate G, B and R segments (Martin, Scottie)
        YUV420,     //!< Robot 36: Y every line, R-Y and B-Y on alternate lines
        YUV422,     //!< Robot 72: Y, R-Y and B-Y every line
        YUVDual     //!< PD: Y of two lines sharing R-Y and B-Y
    };

    enum Component
    {
        CompNone,
        CompY,      //!< Luminance (first line of a PD pair)
        CompY1,     //!< Luminance of the second line of a PD pair
        CompRY,     //!< R-Y chrominance
        CompBY,     //!< B-Y chrominance
        CompChroma, //!< Robot 36 chrominance: R-Y on even lines, B-Y on odd lines
        CompR,
        CompG,
        CompB
    };

    struct Segment
    {
        Component m_component;
        double m_offsetMs;      //!< Start, relative to the start of the sync pulse
        double m_durationMs;
    };

    static const int m_maxSegments = 4;

    Id m_id;
    const char *m_name;
    int m_visCode;
    int m_width;
    int m_height;               //!< Image lines
    int m_linesPerSync;         //!< Image lines per sync period
    ColorFormat m_colorFormat;
    double m_syncMs;            //!< Sync pulse duration
    double m_periodMs;          //!< Time between successive sync pulses
    double m_firstSyncMs;       //!< Time from the end of the VIS code to the first sync pulse of the line structure
    int m_nbSegments;
    Segment m_segments[m_maxSegments];

    int getSyncCount() const { return m_height / m_linesPerSync; }
    double getDurationSeconds() const { return (m_firstSyncMs + getSyncCount() * m_periodMs) / 1000.0; }

    static const SSTVMode& getMode(int index);
    static int getModeIndexFromVIS(int visCode);  //!< -1 if not supported
    static int getModeIndexFromName(const QString& name); //!< -1 if not found
    static QStringList getModeNames();
    static const SSTVMode m_modes[NbModes];
};

#endif // INCLUDE_SSTVMODES_H
