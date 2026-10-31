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

#include "varamodsettings.h"

VARAModSettings::VARAModSettings() :
    m_channelMarker(nullptr),
    m_rollupState(nullptr)
{
    resetToDefaults();
}

void VARAModSettings::resetToDefaults()
{
    m_inputFrequencyOffset = 0;
    m_cmdPort = 8300;
    m_fmDeviation = 3000.0f;
    m_gain = 0.0f;
    m_channelMute = false;
    m_pttEnabled = true;
    m_pttAddress = "192.168.2.1";
    m_pttPort = 7373;
    m_pttLeadMs = 60;
    m_pttTailMs = 50;
    m_txPathMs = 0;
    m_linkFrequencyOffset = false;
    m_rgbColor = QColor(255, 170, 0).rgb();
    m_title = "VARA Modulator";
    m_streamIndex = 0;
    m_useReverseAPI = false;
    m_reverseAPIAddress = "127.0.0.1";
    m_reverseAPIPort = 8888;
    m_reverseAPIDeviceIndex = 0;
    m_reverseAPIChannelIndex = 0;
    m_workspaceIndex = 0;
    m_hidden = false;
}

QByteArray VARAModSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeS64(1, m_inputFrequencyOffset);
    s.writeS32(2, m_cmdPort);
    s.writeReal(3, m_fmDeviation);
    s.writeReal(4, m_gain);
    s.writeBool(5, m_channelMute);
    s.writeBool(6, m_pttEnabled);
    s.writeString(7, m_pttAddress);
    s.writeS32(8, m_pttPort);
    s.writeS32(9, m_pttLeadMs);
    s.writeS32(10, m_pttTailMs);
    s.writeS32(11, m_txPathMs);
    s.writeBool(12, m_linkFrequencyOffset);
    s.writeU32(13, m_rgbColor);
    s.writeString(14, m_title);
    if (m_channelMarker) {
        s.writeBlob(15, m_channelMarker->serialize());
    }

    s.writeS32(16, m_streamIndex);
    s.writeBool(17, m_useReverseAPI);
    s.writeString(18, m_reverseAPIAddress);
    s.writeU32(19, m_reverseAPIPort);
    s.writeU32(20, m_reverseAPIDeviceIndex);
    s.writeU32(21, m_reverseAPIChannelIndex);
    if (m_rollupState) {
        s.writeBlob(22, m_rollupState->serialize());
    }

    s.writeS32(23, m_workspaceIndex);
    s.writeBlob(24, m_geometryBytes);
    s.writeBool(25, m_hidden);

    return s.final();
}

bool VARAModSettings::deserialize(const QByteArray& data)
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

        d.readS64(1, &m_inputFrequencyOffset, 0);
        d.readS32(2, &m_cmdPort, 8300);
        d.readReal(3, &m_fmDeviation, 3000.0f);
        d.readReal(4, &m_gain, 0.0f);
        d.readBool(5, &m_channelMute, false);
        d.readBool(6, &m_pttEnabled, true);
        d.readString(7, &m_pttAddress, "192.168.2.1");
        d.readS32(8, &m_pttPort, 7373);
        d.readS32(9, &m_pttLeadMs, 60);
        d.readS32(10, &m_pttTailMs, 50);
        d.readS32(11, &m_txPathMs, 0);
        d.readBool(12, &m_linkFrequencyOffset, false);
        d.readU32(13, &m_rgbColor, QColor(255, 170, 0).rgb());
        d.readString(14, &m_title, "VARA Modulator");
        if (m_channelMarker)
        {
            d.readBlob(15, &bytetmp);
            m_channelMarker->deserialize(bytetmp);
        }

        d.readS32(16, &m_streamIndex, 0);
        d.readBool(17, &m_useReverseAPI, false);
        d.readString(18, &m_reverseAPIAddress, "127.0.0.1");
        d.readU32(19, &utmp, 0);

        if ((utmp > 1023) && (utmp < 65535)) {
            m_reverseAPIPort = utmp;
        } else {
            m_reverseAPIPort = 8888;
        }

        d.readU32(20, &utmp, 0);
        m_reverseAPIDeviceIndex = utmp > 99 ? 99 : utmp;
        d.readU32(21, &utmp, 0);
        m_reverseAPIChannelIndex = utmp > 99 ? 99 : utmp;
        if (m_rollupState)
        {
            d.readBlob(22, &bytetmp);
            m_rollupState->deserialize(bytetmp);
        }

        d.readS32(23, &m_workspaceIndex, 0);
        d.readBlob(24, &m_geometryBytes);
        d.readBool(25, &m_hidden, false);

        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

void VARAModSettings::applySettings(const QStringList& settingsKeys, const VARAModSettings& settings)
{
    if (settingsKeys.contains("inputFrequencyOffset")) {
        m_inputFrequencyOffset = settings.m_inputFrequencyOffset;
    }
    if (settingsKeys.contains("cmdPort")) {
        m_cmdPort = settings.m_cmdPort;
    }
    if (settingsKeys.contains("fmDeviation")) {
        m_fmDeviation = settings.m_fmDeviation;
    }
    if (settingsKeys.contains("gain")) {
        m_gain = settings.m_gain;
    }
    if (settingsKeys.contains("channelMute")) {
        m_channelMute = settings.m_channelMute;
    }
    if (settingsKeys.contains("pttEnabled")) {
        m_pttEnabled = settings.m_pttEnabled;
    }
    if (settingsKeys.contains("pttAddress")) {
        m_pttAddress = settings.m_pttAddress;
    }
    if (settingsKeys.contains("pttPort")) {
        m_pttPort = settings.m_pttPort;
    }
    if (settingsKeys.contains("pttLeadMs")) {
        m_pttLeadMs = settings.m_pttLeadMs;
    }
    if (settingsKeys.contains("pttTailMs")) {
        m_pttTailMs = settings.m_pttTailMs;
    }
    if (settingsKeys.contains("txPathMs")) {
        m_txPathMs = settings.m_txPathMs;
    }
    if (settingsKeys.contains("linkFrequencyOffset")) {
        m_linkFrequencyOffset = settings.m_linkFrequencyOffset;
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

QString VARAModSettings::getDebugString(const QStringList& settingsKeys, bool force) const
{
    QStringList parts;

    if (settingsKeys.contains("inputFrequencyOffset") || force) {
        parts.append(QString("m_inputFrequencyOffset: %1").arg(m_inputFrequencyOffset));
    }
    if (settingsKeys.contains("cmdPort") || force) {
        parts.append(QString("m_cmdPort: %1").arg(m_cmdPort));
    }
    if (settingsKeys.contains("fmDeviation") || force) {
        parts.append(QString("m_fmDeviation: %1").arg(m_fmDeviation));
    }
    if (settingsKeys.contains("gain") || force) {
        parts.append(QString("m_gain: %1").arg(m_gain));
    }
    if (settingsKeys.contains("channelMute") || force) {
        parts.append(QString("m_channelMute: %1").arg(m_channelMute));
    }
    if (settingsKeys.contains("pttEnabled") || force) {
        parts.append(QString("m_pttEnabled: %1").arg(m_pttEnabled));
    }
    if (settingsKeys.contains("pttAddress") || force) {
        parts.append(QString("m_pttAddress: %1").arg(m_pttAddress));
    }
    if (settingsKeys.contains("pttPort") || force) {
        parts.append(QString("m_pttPort: %1").arg(m_pttPort));
    }
    if (settingsKeys.contains("pttLeadMs") || force) {
        parts.append(QString("m_pttLeadMs: %1").arg(m_pttLeadMs));
    }
    if (settingsKeys.contains("pttTailMs") || force) {
        parts.append(QString("m_pttTailMs: %1").arg(m_pttTailMs));
    }
    if (settingsKeys.contains("txPathMs") || force) {
        parts.append(QString("m_txPathMs: %1").arg(m_txPathMs));
    }
    if (settingsKeys.contains("linkFrequencyOffset") || force) {
        parts.append(QString("m_linkFrequencyOffset: %1").arg(m_linkFrequencyOffset));
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
