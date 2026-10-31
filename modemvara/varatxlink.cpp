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

#include <QMap>

#include "varatxlink.h"

namespace {

QMutex registryMutex;
QMap<quint16, std::weak_ptr<VARATxLink>> registry;

}

VARATxLink::VARATxLink() :
    m_ring(m_capacity, 0.0f),
    m_head(0),
    m_fill(0),
    m_target(0),
    m_window(0),
    m_txPath(0),
    m_keyed(false),
    m_engineAttached(false),
    m_engineMode(0),
    m_underflows(0)
{
}

VARATxLink::~VARATxLink()
{
}

std::shared_ptr<VARATxLink> VARATxLink::create(quint16 cmdPort)
{
    auto link = std::make_shared<VARATxLink>();
    QMutexLocker lock(&registryMutex);
    registry[cmdPort] = link;
    return link;
}

std::shared_ptr<VARATxLink> VARATxLink::find(quint16 cmdPort)
{
    QMutexLocker lock(&registryMutex);
    auto it = registry.find(cmdPort);

    if (it == registry.end()) {
        return nullptr;
    }

    auto link = it->lock();

    if (!link) {
        registry.erase(it);
    }

    return link;
}

void VARATxLink::setTiming(int leadSamples, int windowSamples, int txPathSamples)
{
    QMutexLocker lock(&m_mutex);
    m_target = std::clamp(leadSamples, 0, m_capacity / 2);
    m_window = std::clamp(windowSamples, 0, m_capacity / 4);
    m_txPath = std::max(0, txPathSamples);
}

void VARATxLink::setPttCallback(std::function<void(bool)> callback)
{
    QMutexLocker lock(&m_mutex);
    m_pttCallback = callback;
}

void VARATxLink::pull(float *out, int n)
{
    QMutexLocker lock(&m_mutex);
    int available = std::min(n, m_fill);

    for (int i = 0; i < available; i++) {
        out[i] = read();
    }

    std::fill(out + available, out + n, 0.0f);

    if ((available < n) && m_keyed) {
        m_underflows++;
    }
}

bool VARATxLink::getKeyed() const
{
    QMutexLocker lock(&m_mutex);
    return m_keyed;
}

bool VARATxLink::getEngineAttached() const
{
    QMutexLocker lock(&m_mutex);
    return m_engineAttached;
}

int VARATxLink::getEngineMode() const
{
    QMutexLocker lock(&m_mutex);
    return m_engineMode;
}

int VARATxLink::getFill() const
{
    QMutexLocker lock(&m_mutex);
    return m_fill;
}

unsigned int VARATxLink::getUnderflows() const
{
    QMutexLocker lock(&m_mutex);
    return m_underflows;
}

void VARATxLink::push(const float *in, int n, bool keyed)
{
    std::function<void(bool)> callback;
    bool edge;

    {
        QMutexLocker lock(&m_mutex);
        edge = keyed != m_keyed;
        m_keyed = keyed;

        for (int i = 0; i < n; i++) {
            write(in[i]);
        }

        if (!keyed)
        {
            while (m_fill < m_target) {
                write(0.0f);
            }

            if (m_fill > m_target + m_window) {
                trimSilence();
            }
        }

        if (edge) {
            callback = m_pttCallback;
        }
    }

    // Outside the lock, as the callback may post to another thread
    if (callback) {
        callback(keyed);
    }
}

qint64 VARATxLink::getTxLatency() const
{
    QMutexLocker lock(&m_mutex);
    return m_target + m_window / 2 + m_txPath;
}

void VARATxLink::setEngine(bool attached, int mode)
{
    QMutexLocker lock(&m_mutex);
    m_engineAttached = attached;
    m_engineMode = mode;
}

void VARATxLink::write(float v)
{
    if (m_fill == m_capacity) { // overflow: drop the oldest
        m_fill--;
    }

    m_ring[m_head] = v;
    m_head = (m_head + 1) % m_capacity;
    m_fill++;
}

float VARATxLink::read()
{
    float v = m_ring[(m_head - m_fill + m_capacity) % m_capacity];
    m_fill--;
    return v;
}

float VARATxLink::peek(int i) const
{
    return m_ring[(m_head - m_fill + i + m_capacity) % m_capacity];
}

void VARATxLink::trimSilence()
{
    // The tail of the last transmission may still be queued, so only drop silence ahead of it
    while ((m_fill > m_target) && (peek(0) == 0.0f)) {
        m_fill--;
    }
}
