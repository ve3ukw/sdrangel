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

#include "sstvmodes.h"

// PD modes: 20 ms sync, 2.08 ms porch, then Y, R-Y, B-Y and Y of the next line
#define PD_MODE(ID, NAME, VIS, WIDTH, HEIGHT, COMP) \
    { SSTVMode::ID, NAME, VIS, WIDTH, HEIGHT, 2, SSTVMode::YUVDual, 20.0, 22.08 + 4.0 * (COMP), 0.0, 4, { \
        { SSTVMode::CompY, 22.08, COMP }, \
        { SSTVMode::CompRY, 22.08 + (COMP), COMP }, \
        { SSTVMode::CompBY, 22.08 + 2.0 * (COMP), COMP }, \
        { SSTVMode::CompY1, 22.08 + 3.0 * (COMP), COMP } } }

// Martin modes: 4.862 ms sync, then G, B and R, each preceded by a 0.572 ms separator
#define MARTIN_MODE(ID, NAME, VIS, COMP) \
    { SSTVMode::ID, NAME, VIS, 320, 256, 1, SSTVMode::RGB, 4.862, 4.862 + 4.0 * 0.572 + 3.0 * (COMP), 0.0, 3, { \
        { SSTVMode::CompG, 4.862 + 0.572, COMP }, \
        { SSTVMode::CompB, 4.862 + 2.0 * 0.572 + (COMP), COMP }, \
        { SSTVMode::CompR, 4.862 + 3.0 * 0.572 + 2.0 * (COMP), COMP }, \
        { SSTVMode::CompNone, 0.0, 0.0 } } }

// Scottie modes: 1.5 ms separator, G, 1.5 ms separator, B, 9 ms sync, 1.5 ms porch, R.
// The transmission starts with an extra sync pulse, before the G of the first line.
#define SCOTTIE_MODE(ID, NAME, VIS, COMP) \
    { SSTVMode::ID, NAME, VIS, 320, 256, 1, SSTVMode::RGB, 9.0, 9.0 + 3.0 * 1.5 + 3.0 * (COMP), 9.0 + 2.0 * 1.5 + 2.0 * (COMP), 3, { \
        { SSTVMode::CompG, -1.5 - 2.0 * (COMP), COMP }, \
        { SSTVMode::CompB, -(COMP), COMP }, \
        { SSTVMode::CompR, 9.0 + 1.5, COMP }, \
        { SSTVMode::CompNone, 0.0, 0.0 } } }

const SSTVMode SSTVMode::m_modes[SSTVMode::NbModes] = {
    // Robot 36: 9 ms sync, 3 ms porch, 88 ms Y, 4.5 ms separator, 1.5 ms porch, 44 ms chrominance
    { SSTVMode::Robot36, "Robot 36", 8, 320, 240, 1, SSTVMode::YUV420, 9.0, 150.0, 0.0, 2, {
        { SSTVMode::CompY, 12.0, 88.0 },
        { SSTVMode::CompChroma, 106.0, 44.0 },
        { SSTVMode::CompNone, 0.0, 0.0 },
        { SSTVMode::CompNone, 0.0, 0.0 } } },
    // Robot 72: 9 ms sync, 3 ms porch, 138 ms Y, then R-Y and B-Y of 69 ms, each after a 4.5 ms separator and 1.5 ms porch
    { SSTVMode::Robot72, "Robot 72", 12, 320, 240, 1, SSTVMode::YUV422, 9.0, 300.0, 0.0, 3, {
        { SSTVMode::CompY, 12.0, 138.0 },
        { SSTVMode::CompRY, 156.0, 69.0 },
        { SSTVMode::CompBY, 231.0, 69.0 },
        { SSTVMode::CompNone, 0.0, 0.0 } } },
    PD_MODE(PD50, "PD 50", 93, 320, 256, 91.52),
    PD_MODE(PD90, "PD 90", 99, 320, 256, 170.24),
    PD_MODE(PD120, "PD 120", 95, 640, 496, 121.6),
    PD_MODE(PD160, "PD 160", 98, 512, 400, 195.584),
    PD_MODE(PD180, "PD 180", 96, 640, 496, 183.04),
    PD_MODE(PD240, "PD 240", 97, 640, 496, 244.48),
    PD_MODE(PD290, "PD 290", 94, 800, 616, 228.8),
    MARTIN_MODE(Martin1, "Martin 1", 44, 146.432),
    MARTIN_MODE(Martin2, "Martin 2", 40, 73.216),
    SCOTTIE_MODE(Scottie1, "Scottie 1", 60, 138.24),
    SCOTTIE_MODE(Scottie2, "Scottie 2", 56, 88.064),
    SCOTTIE_MODE(ScottieDX, "Scottie DX", 76, 345.6)
};

const SSTVMode& SSTVMode::getMode(int index)
{
    if ((index < 0) || (index >= NbModes)) {
        return m_modes[0];
    }

    return m_modes[index];
}

int SSTVMode::getModeIndexFromVIS(int visCode)
{
    for (int i = 0; i < NbModes; i++)
    {
        if (m_modes[i].m_visCode == visCode) {
            return i;
        }
    }

    return -1;
}

int SSTVMode::getModeIndexFromName(const QString& name)
{
    for (int i = 0; i < NbModes; i++)
    {
        if (name.compare(m_modes[i].m_name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }

    return -1;
}

QStringList SSTVMode::getModeNames()
{
    QStringList names;

    for (int i = 0; i < NbModes; i++) {
        names.append(m_modes[i].m_name);
    }

    return names;
}
