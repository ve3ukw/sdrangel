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

#include <QColor>

#include "audio/audiodevicemanager.h"
#include "settings/serializable.h"
#include "util/simpleserializer.h"

#include "sstvmodes.h"
#include "sstvdemodsettings.h"

SSTVDemodSettings::SSTVDemodSettings() :
    m_channelMarker(nullptr),
    m_rollupState(nullptr)
{
    resetToDefaults();
}

void SSTVDemodSettings::resetToDefaults()
{
    m_inputFrequencyOffset = 0;
    m_rfBandwidth = 15000.0f;
    m_fmDeviation = 5000.0f;
    m_autoMode = true;
    m_mode = SSTVMode::PD120;
    m_autoSave = false;
    m_autoSavePath = "";
    m_volume = 1.0f;
    m_audioMute = false;
    m_audioDeviceName = AudioDeviceManager::m_defaultDeviceName;
    m_rgbColor = QColor(200, 120, 255).rgb();
    m_title = "SSTV Demodulator";
    m_streamIndex = 0;
    m_useReverseAPI = false;
    m_reverseAPIAddress = "127.0.0.1";
    m_reverseAPIPort = 8888;
    m_reverseAPIDeviceIndex = 0;
    m_reverseAPIChannelIndex = 0;
    m_workspaceIndex = 0;
    m_hidden = false;
}

QByteArray SSTVDemodSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeS32(1, m_inputFrequencyOffset);
    s.writeReal(2, m_rfBandwidth);
    s.writeReal(3, m_fmDeviation);
    s.writeBool(4, m_autoMode);
    s.writeS32(5, m_mode);
    s.writeBool(6, m_autoSave);
    s.writeString(7, m_autoSavePath);
    s.writeReal(8, m_volume);
    s.writeBool(9, m_audioMute);
    s.writeString(10, m_audioDeviceName);
    s.writeU32(11, m_rgbColor);
    s.writeString(12, m_title);

    if (m_channelMarker) {
        s.writeBlob(13, m_channelMarker->serialize());
    }

    s.writeS32(14, m_streamIndex);
    s.writeBool(15, m_useReverseAPI);
    s.writeString(16, m_reverseAPIAddress);
    s.writeU32(17, m_reverseAPIPort);
    s.writeU32(18, m_reverseAPIDeviceIndex);
    s.writeU32(19, m_reverseAPIChannelIndex);

    if (m_rollupState) {
        s.writeBlob(20, m_rollupState->serialize());
    }

    s.writeS32(21, m_workspaceIndex);
    s.writeBlob(22, m_geometryBytes);
    s.writeBool(23, m_hidden);

    return s.final();
}

bool SSTVDemodSettings::deserialize(const QByteArray& data)
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

        d.readS32(1, &m_inputFrequencyOffset, 0);
        d.readReal(2, &m_rfBandwidth, 15000.0f);
        d.readReal(3, &m_fmDeviation, 5000.0f);
        d.readBool(4, &m_autoMode, true);
        d.readS32(5, &m_mode, SSTVMode::PD120);

        if ((m_mode < 0) || (m_mode >= SSTVMode::NbModes)) {
            m_mode = SSTVMode::PD120;
        }

        d.readBool(6, &m_autoSave, false);
        d.readString(7, &m_autoSavePath, "");
        d.readReal(8, &m_volume, 1.0f);
        d.readBool(9, &m_audioMute, false);
        d.readString(10, &m_audioDeviceName, AudioDeviceManager::m_defaultDeviceName);
        d.readU32(11, &m_rgbColor, QColor(200, 120, 255).rgb());
        d.readString(12, &m_title, "SSTV Demodulator");

        if (m_channelMarker)
        {
            d.readBlob(13, &bytetmp);
            m_channelMarker->deserialize(bytetmp);
        }

        d.readS32(14, &m_streamIndex, 0);
        d.readBool(15, &m_useReverseAPI, false);
        d.readString(16, &m_reverseAPIAddress, "127.0.0.1");
        d.readU32(17, &utmp, 0);

        if ((utmp > 1023) && (utmp < 65535)) {
            m_reverseAPIPort = utmp;
        } else {
            m_reverseAPIPort = 8888;
        }

        d.readU32(18, &utmp, 0);
        m_reverseAPIDeviceIndex = utmp > 99 ? 99 : utmp;
        d.readU32(19, &utmp, 0);
        m_reverseAPIChannelIndex = utmp > 99 ? 99 : utmp;

        if (m_rollupState)
        {
            d.readBlob(20, &bytetmp);
            m_rollupState->deserialize(bytetmp);
        }

        d.readS32(21, &m_workspaceIndex, 0);
        d.readBlob(22, &m_geometryBytes);
        d.readBool(23, &m_hidden, false);

        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

void SSTVDemodSettings::applySettings(const QStringList& settingsKeys, const SSTVDemodSettings& settings)
{
    if (settingsKeys.contains("inputFrequencyOffset")) {
        m_inputFrequencyOffset = settings.m_inputFrequencyOffset;
    }
    if (settingsKeys.contains("rfBandwidth")) {
        m_rfBandwidth = settings.m_rfBandwidth;
    }
    if (settingsKeys.contains("fmDeviation")) {
        m_fmDeviation = settings.m_fmDeviation;
    }
    if (settingsKeys.contains("autoMode")) {
        m_autoMode = settings.m_autoMode;
    }
    if (settingsKeys.contains("mode")) {
        m_mode = settings.m_mode;
    }
    if (settingsKeys.contains("autoSave")) {
        m_autoSave = settings.m_autoSave;
    }
    if (settingsKeys.contains("autoSavePath")) {
        m_autoSavePath = settings.m_autoSavePath;
    }
    if (settingsKeys.contains("volume")) {
        m_volume = settings.m_volume;
    }
    if (settingsKeys.contains("audioMute")) {
        m_audioMute = settings.m_audioMute;
    }
    if (settingsKeys.contains("audioDeviceName")) {
        m_audioDeviceName = settings.m_audioDeviceName;
    }
    if (settingsKeys.contains("rgbColor")) {
        m_rgbColor = settings.m_rgbColor;
    }
    if (settingsKeys.contains("title")) {
        m_title = settings.m_title;
    }
    if (settingsKeys.contains("streamIndex")) {
        m_streamIndex = settings.m_streamIndex;
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
    if (settingsKeys.contains("reverseAPIDeviceIndex")) {
        m_reverseAPIDeviceIndex = settings.m_reverseAPIDeviceIndex;
    }
    if (settingsKeys.contains("reverseAPIChannelIndex")) {
        m_reverseAPIChannelIndex = settings.m_reverseAPIChannelIndex;
    }
    if (settingsKeys.contains("workspaceIndex")) {
        m_workspaceIndex = settings.m_workspaceIndex;
    }
    if (settingsKeys.contains("geometryBytes")) {
        m_geometryBytes = settings.m_geometryBytes;
    }
    if (settingsKeys.contains("hidden")) {
        m_hidden = settings.m_hidden;
    }
}

QString SSTVDemodSettings::getDebugString(const QStringList& settingsKeys, bool force) const
{
    QStringList parts;

    if (settingsKeys.contains("inputFrequencyOffset") || force) {
        parts.append(QString("m_inputFrequencyOffset: %1").arg(m_inputFrequencyOffset));
    }
    if (settingsKeys.contains("rfBandwidth") || force) {
        parts.append(QString("m_rfBandwidth: %1").arg(m_rfBandwidth));
    }
    if (settingsKeys.contains("fmDeviation") || force) {
        parts.append(QString("m_fmDeviation: %1").arg(m_fmDeviation));
    }
    if (settingsKeys.contains("autoMode") || force) {
        parts.append(QString("m_autoMode: %1").arg(m_autoMode));
    }
    if (settingsKeys.contains("mode") || force) {
        parts.append(QString("m_mode: %1").arg(m_mode));
    }
    if (settingsKeys.contains("autoSave") || force) {
        parts.append(QString("m_autoSave: %1").arg(m_autoSave));
    }
    if (settingsKeys.contains("autoSavePath") || force) {
        parts.append(QString("m_autoSavePath: %1").arg(m_autoSavePath));
    }
    if (settingsKeys.contains("volume") || force) {
        parts.append(QString("m_volume: %1").arg(m_volume));
    }
    if (settingsKeys.contains("audioMute") || force) {
        parts.append(QString("m_audioMute: %1").arg(m_audioMute));
    }
    if (settingsKeys.contains("audioDeviceName") || force) {
        parts.append(QString("m_audioDeviceName: %1").arg(m_audioDeviceName));
    }
    if (settingsKeys.contains("rgbColor") || force) {
        parts.append(QString("m_rgbColor: %1").arg(m_rgbColor));
    }
    if (settingsKeys.contains("title") || force) {
        parts.append(QString("m_title: %1").arg(m_title));
    }
    if (settingsKeys.contains("streamIndex") || force) {
        parts.append(QString("m_streamIndex: %1").arg(m_streamIndex));
    }
    if (settingsKeys.contains("useReverseAPI") || force) {
        parts.append(QString("m_useReverseAPI: %1").arg(m_useReverseAPI));
    }
    if (settingsKeys.contains("reverseAPIAddress") || force) {
        parts.append(QString("m_reverseAPIAddress: %1").arg(m_reverseAPIAddress));
    }
    if (settingsKeys.contains("reverseAPIPort") || force) {
        parts.append(QString("m_reverseAPIPort: %1").arg(m_reverseAPIPort));
    }
    if (settingsKeys.contains("reverseAPIDeviceIndex") || force) {
        parts.append(QString("m_reverseAPIDeviceIndex: %1").arg(m_reverseAPIDeviceIndex));
    }
    if (settingsKeys.contains("reverseAPIChannelIndex") || force) {
        parts.append(QString("m_reverseAPIChannelIndex: %1").arg(m_reverseAPIChannelIndex));
    }
    if (settingsKeys.contains("workspaceIndex") || force) {
        parts.append(QString("m_workspaceIndex: %1").arg(m_workspaceIndex));
    }
    if (settingsKeys.contains("hidden") || force) {
        parts.append(QString("m_hidden: %1").arg(m_hidden));
    }

    return " " + parts.join(" ");
}
