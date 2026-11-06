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

#include <sstream>

#include <QColor>

#include "util/simpleserializer.h"
#include "settings/serializable.h"

#include "jogwheelsettings.h"

const QVector<int> JogWheelSettings::m_steps = {1, 5, 10, 25, 50, 100, 500, 1000, 5000, 10000, 100000};

JogWheelSettings::JogWheelSettings() :
    m_rollupState(nullptr)
{
    resetToDefaults();
}

void JogWheelSettings::resetToDefaults()
{
    m_title = "Jog Wheel";
    m_rgbColor = QColor(212, 160, 23).rgb();
    m_target.clear();
    m_linkedTarget.clear();
    m_linkInverted = true;
    m_stepHz = 10;
    m_acceleration = true;
    m_udpEnabled = false;
    m_udpAddress = "127.0.0.1";
    m_udpPort = 9988;
    m_useReverseAPI = false;
    m_reverseAPIAddress = "127.0.0.1";
    m_reverseAPIPort = 8888;
    m_reverseAPIFeatureSetIndex = 0;
    m_reverseAPIFeatureIndex = 0;
    m_workspaceIndex = 0;
}

QByteArray JogWheelSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeString(1, m_title);
    s.writeU32(2, m_rgbColor);
    s.writeString(3, m_target);
    s.writeString(4, m_linkedTarget);
    s.writeBool(5, m_linkInverted);
    s.writeS32(6, m_stepHz);
    s.writeBool(7, m_acceleration);
    s.writeBool(8, m_udpEnabled);
    s.writeString(9, m_udpAddress);
    s.writeU32(10, m_udpPort);
    s.writeBool(11, m_useReverseAPI);
    s.writeString(12, m_reverseAPIAddress);
    s.writeU32(13, m_reverseAPIPort);
    s.writeU32(14, m_reverseAPIFeatureSetIndex);
    s.writeU32(15, m_reverseAPIFeatureIndex);

    if (m_rollupState) {
        s.writeBlob(16, m_rollupState->serialize());
    }

    s.writeS32(17, m_workspaceIndex);
    s.writeBlob(18, m_geometryBytes);

    return s.final();
}

bool JogWheelSettings::deserialize(const QByteArray& data)
{
    SimpleDeserializer d(data);

    if (!d.isValid())
    {
        resetToDefaults();
        return false;
    }

    if (d.getVersion() == 1)
    {
        QByteArray bytetmp;
        uint32_t utmp;

        d.readString(1, &m_title, "Jog Wheel");
        d.readU32(2, &m_rgbColor, QColor(212, 160, 23).rgb());
        d.readString(3, &m_target, "");
        d.readString(4, &m_linkedTarget, "");
        d.readBool(5, &m_linkInverted, true);
        d.readS32(6, &m_stepHz, 10);
        d.readBool(7, &m_acceleration, true);
        d.readBool(8, &m_udpEnabled, false);
        d.readString(9, &m_udpAddress, "127.0.0.1");
        d.readU32(10, &utmp, 9988);
        m_udpPort = utmp > 1023 && utmp < 65536 ? utmp : 9988;
        d.readBool(11, &m_useReverseAPI, false);
        d.readString(12, &m_reverseAPIAddress, "127.0.0.1");
        d.readU32(13, &utmp, 0);
        m_reverseAPIPort = utmp > 1023 && utmp < 65535 ? utmp : 8888;
        d.readU32(14, &utmp, 0);
        m_reverseAPIFeatureSetIndex = utmp > 99 ? 99 : utmp;
        d.readU32(15, &utmp, 0);
        m_reverseAPIFeatureIndex = utmp > 99 ? 99 : utmp;

        if (m_rollupState)
        {
            d.readBlob(16, &bytetmp);
            m_rollupState->deserialize(bytetmp);
        }

        d.readS32(17, &m_workspaceIndex, 0);
        d.readBlob(18, &m_geometryBytes);

        if (m_stepHz < 1) {
            m_stepHz = 1;
        }

        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

void JogWheelSettings::applySettings(const QStringList& settingsKeys, const JogWheelSettings& settings)
{
    if (settingsKeys.contains("title")) {
        m_title = settings.m_title;
    }
    if (settingsKeys.contains("rgbColor")) {
        m_rgbColor = settings.m_rgbColor;
    }
    if (settingsKeys.contains("target")) {
        m_target = settings.m_target;
    }
    if (settingsKeys.contains("linkedTarget")) {
        m_linkedTarget = settings.m_linkedTarget;
    }
    if (settingsKeys.contains("linkInverted")) {
        m_linkInverted = settings.m_linkInverted;
    }
    if (settingsKeys.contains("stepHz")) {
        m_stepHz = settings.m_stepHz;
    }
    if (settingsKeys.contains("acceleration")) {
        m_acceleration = settings.m_acceleration;
    }
    if (settingsKeys.contains("udpEnabled")) {
        m_udpEnabled = settings.m_udpEnabled;
    }
    if (settingsKeys.contains("udpAddress")) {
        m_udpAddress = settings.m_udpAddress;
    }
    if (settingsKeys.contains("udpPort")) {
        m_udpPort = settings.m_udpPort;
    }
    if (settingsKeys.contains("useReverseAPI")) {
        m_useReverseAPI = settings.m_useReverseAPI;
    }
    if (settingsKeys.contains("reverseAPIAddress")) {
        m_reverseAPIAddress = settings.m_reverseAPIAddress;
    }
    if (settingsKeys.contains("reverseAPIPort")) {
        m_reverseAPIPort = settings.m_reverseAPIPort;
    }
    if (settingsKeys.contains("reverseAPIFeatureSetIndex")) {
        m_reverseAPIFeatureSetIndex = settings.m_reverseAPIFeatureSetIndex;
    }
    if (settingsKeys.contains("reverseAPIFeatureIndex")) {
        m_reverseAPIFeatureIndex = settings.m_reverseAPIFeatureIndex;
    }
    if (settingsKeys.contains("workspaceIndex")) {
        m_workspaceIndex = settings.m_workspaceIndex;
    }
}

QString JogWheelSettings::getDebugString(const QStringList& settingsKeys, bool force) const
{
    std::ostringstream ostr;

    if (settingsKeys.contains("title") || force) {
        ostr << " m_title: " << m_title.toStdString();
    }
    if (settingsKeys.contains("target") || force) {
        ostr << " m_target: " << m_target.toStdString();
    }
    if (settingsKeys.contains("linkedTarget") || force) {
        ostr << " m_linkedTarget: " << m_linkedTarget.toStdString();
    }
    if (settingsKeys.contains("linkInverted") || force) {
        ostr << " m_linkInverted: " << m_linkInverted;
    }
    if (settingsKeys.contains("stepHz") || force) {
        ostr << " m_stepHz: " << m_stepHz;
    }
    if (settingsKeys.contains("acceleration") || force) {
        ostr << " m_acceleration: " << m_acceleration;
    }
    if (settingsKeys.contains("udpEnabled") || force) {
        ostr << " m_udpEnabled: " << m_udpEnabled;
    }
    if (settingsKeys.contains("udpAddress") || force) {
        ostr << " m_udpAddress: " << m_udpAddress.toStdString();
    }
    if (settingsKeys.contains("udpPort") || force) {
        ostr << " m_udpPort: " << m_udpPort;
    }

    return QString(ostr.str().c_str());
}
