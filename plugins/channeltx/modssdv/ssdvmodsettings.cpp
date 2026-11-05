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

#include "settings/serializable.h"
#include "util/simpleserializer.h"

#include "ssdvmodsettings.h"

SSDVModSettings::SSDVModSettings() :
    m_channelMarker(nullptr),
    m_rollupState(nullptr)
{
    resetToDefaults();
}

void SSDVModSettings::resetToDefaults()
{
    m_inputFrequencyOffset = 0;
    m_baudRate = 9600;
    m_rrcRolloff = 0.35f;
    m_gain = -3.0f;
    m_channelMute = false;
    m_frameSize = 223;
    m_rs = true;
    m_dualBasis = false;
    m_randomizer = true;
    m_differential = true;
    m_convolutional = true;
    m_packetFormat = AO123;
    m_callsign = "MYCALL";
    m_spacecraftId = 0;
    m_virtualChannel = 1;
    m_imageFilename = "";
    m_imageId = 0;
    m_quality = 4;
    m_width = 320;
    m_height = 256;
    m_fill = false;
    m_preambleMs = 1000;
    m_repeat = false;
    m_repeatDelay = 10.0f;
    m_rgbColor = QColor(0, 200, 255).rgb();
    m_title = "SSDV Modulator";
    m_streamIndex = 0;
    m_useReverseAPI = false;
    m_reverseAPIAddress = "127.0.0.1";
    m_reverseAPIPort = 8888;
    m_reverseAPIDeviceIndex = 0;
    m_reverseAPIChannelIndex = 0;
    m_workspaceIndex = 0;
    m_hidden = false;
}

QByteArray SSDVModSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeS64(1, m_inputFrequencyOffset);
    s.writeS32(2, m_baudRate);
    s.writeReal(3, m_rrcRolloff);
    s.writeReal(4, m_gain);
    s.writeBool(5, m_channelMute);
    s.writeS32(6, m_frameSize);
    s.writeBool(7, m_rs);
    s.writeBool(8, m_dualBasis);
    s.writeBool(9, m_randomizer);
    s.writeBool(10, m_differential);
    s.writeBool(11, m_convolutional);
    s.writeS32(12, m_packetFormat);
    s.writeString(13, m_callsign);
    s.writeS32(14, m_spacecraftId);
    s.writeS32(15, m_virtualChannel);
    s.writeString(16, m_imageFilename);
    s.writeS32(17, m_imageId);
    s.writeS32(18, m_quality);
    s.writeS32(19, m_width);
    s.writeS32(20, m_height);
    s.writeBool(21, m_fill);
    s.writeS32(22, m_preambleMs);
    s.writeBool(23, m_repeat);
    s.writeReal(24, m_repeatDelay);
    s.writeU32(25, m_rgbColor);
    s.writeString(26, m_title);
    if (m_channelMarker) {
        s.writeBlob(27, m_channelMarker->serialize());
    }

    s.writeS32(28, m_streamIndex);
    s.writeBool(29, m_useReverseAPI);
    s.writeString(30, m_reverseAPIAddress);
    s.writeU32(31, m_reverseAPIPort);
    s.writeU32(32, m_reverseAPIDeviceIndex);
    s.writeU32(33, m_reverseAPIChannelIndex);
    if (m_rollupState) {
        s.writeBlob(34, m_rollupState->serialize());
    }

    s.writeS32(35, m_workspaceIndex);
    s.writeBlob(36, m_geometryBytes);
    s.writeBool(37, m_hidden);

    return s.final();
}

bool SSDVModSettings::deserialize(const QByteArray& data)
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
        d.readS32(2, &m_baudRate, 9600);
        d.readReal(3, &m_rrcRolloff, 0.35f);
        d.readReal(4, &m_gain, -3.0f);
        d.readBool(5, &m_channelMute, false);
        d.readS32(6, &m_frameSize, 223);
        d.readBool(7, &m_rs, true);
        d.readBool(8, &m_dualBasis, false);
        d.readBool(9, &m_randomizer, true);
        d.readBool(10, &m_differential, true);
        d.readBool(11, &m_convolutional, true);
        d.readS32(12, &m_packetFormat, AO123);
        d.readString(13, &m_callsign, "MYCALL");
        d.readS32(14, &m_spacecraftId, 0);
        d.readS32(15, &m_virtualChannel, 1);
        d.readString(16, &m_imageFilename, "");
        d.readS32(17, &m_imageId, 0);
        d.readS32(18, &m_quality, 4);
        d.readS32(19, &m_width, 320);
        d.readS32(20, &m_height, 256);
        d.readBool(21, &m_fill, false);
        d.readS32(22, &m_preambleMs, 1000);
        d.readBool(23, &m_repeat, false);
        d.readReal(24, &m_repeatDelay, 10.0f);
        d.readU32(25, &m_rgbColor, QColor(0, 200, 255).rgb());
        d.readString(26, &m_title, "SSDV Modulator");
        if (m_channelMarker)
        {
            d.readBlob(27, &bytetmp);
            m_channelMarker->deserialize(bytetmp);
        }

        d.readS32(28, &m_streamIndex, 0);
        d.readBool(29, &m_useReverseAPI, false);
        d.readString(30, &m_reverseAPIAddress, "127.0.0.1");
        d.readU32(31, &utmp, 0);

        if ((utmp > 1023) && (utmp < 65535)) {
            m_reverseAPIPort = utmp;
        } else {
            m_reverseAPIPort = 8888;
        }

        d.readU32(32, &utmp, 0);
        m_reverseAPIDeviceIndex = utmp > 99 ? 99 : utmp;
        d.readU32(33, &utmp, 0);
        m_reverseAPIChannelIndex = utmp > 99 ? 99 : utmp;
        if (m_rollupState)
        {
            d.readBlob(34, &bytetmp);
            m_rollupState->deserialize(bytetmp);
        }

        d.readS32(35, &m_workspaceIndex, 0);
        d.readBlob(36, &m_geometryBytes);
        d.readBool(37, &m_hidden, false);

        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

void SSDVModSettings::applySettings(const QStringList& settingsKeys, const SSDVModSettings& settings)
{
    if (settingsKeys.contains("inputFrequencyOffset")) {
        m_inputFrequencyOffset = settings.m_inputFrequencyOffset;
    }
    if (settingsKeys.contains("baudRate")) {
        m_baudRate = settings.m_baudRate;
    }
    if (settingsKeys.contains("rrcRolloff")) {
        m_rrcRolloff = settings.m_rrcRolloff;
    }
    if (settingsKeys.contains("gain")) {
        m_gain = settings.m_gain;
    }
    if (settingsKeys.contains("channelMute")) {
        m_channelMute = settings.m_channelMute;
    }
    if (settingsKeys.contains("frameSize")) {
        m_frameSize = settings.m_frameSize;
    }
    if (settingsKeys.contains("rs")) {
        m_rs = settings.m_rs;
    }
    if (settingsKeys.contains("dualBasis")) {
        m_dualBasis = settings.m_dualBasis;
    }
    if (settingsKeys.contains("randomizer")) {
        m_randomizer = settings.m_randomizer;
    }
    if (settingsKeys.contains("differential")) {
        m_differential = settings.m_differential;
    }
    if (settingsKeys.contains("convolutional")) {
        m_convolutional = settings.m_convolutional;
    }
    if (settingsKeys.contains("packetFormat")) {
        m_packetFormat = settings.m_packetFormat;
    }
    if (settingsKeys.contains("callsign")) {
        m_callsign = settings.m_callsign;
    }
    if (settingsKeys.contains("spacecraftId")) {
        m_spacecraftId = settings.m_spacecraftId;
    }
    if (settingsKeys.contains("virtualChannel")) {
        m_virtualChannel = settings.m_virtualChannel;
    }
    if (settingsKeys.contains("imageFilename")) {
        m_imageFilename = settings.m_imageFilename;
    }
    if (settingsKeys.contains("imageId")) {
        m_imageId = settings.m_imageId;
    }
    if (settingsKeys.contains("quality")) {
        m_quality = settings.m_quality;
    }
    if (settingsKeys.contains("width")) {
        m_width = settings.m_width;
    }
    if (settingsKeys.contains("height")) {
        m_height = settings.m_height;
    }
    if (settingsKeys.contains("fill")) {
        m_fill = settings.m_fill;
    }
    if (settingsKeys.contains("preambleMs")) {
        m_preambleMs = settings.m_preambleMs;
    }
    if (settingsKeys.contains("repeat")) {
        m_repeat = settings.m_repeat;
    }
    if (settingsKeys.contains("repeatDelay")) {
        m_repeatDelay = settings.m_repeatDelay;
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

QString SSDVModSettings::getDebugString(const QStringList& settingsKeys, bool force) const
{
    QStringList parts;

    if (settingsKeys.contains("inputFrequencyOffset") || force) {
        parts.append(QString("m_inputFrequencyOffset: %1").arg(m_inputFrequencyOffset));
    }
    if (settingsKeys.contains("baudRate") || force) {
        parts.append(QString("m_baudRate: %1").arg(m_baudRate));
    }
    if (settingsKeys.contains("rrcRolloff") || force) {
        parts.append(QString("m_rrcRolloff: %1").arg(m_rrcRolloff));
    }
    if (settingsKeys.contains("gain") || force) {
        parts.append(QString("m_gain: %1").arg(m_gain));
    }
    if (settingsKeys.contains("channelMute") || force) {
        parts.append(QString("m_channelMute: %1").arg(m_channelMute));
    }
    if (settingsKeys.contains("frameSize") || force) {
        parts.append(QString("m_frameSize: %1").arg(m_frameSize));
    }
    if (settingsKeys.contains("rs") || force) {
        parts.append(QString("m_rs: %1").arg(m_rs));
    }
    if (settingsKeys.contains("dualBasis") || force) {
        parts.append(QString("m_dualBasis: %1").arg(m_dualBasis));
    }
    if (settingsKeys.contains("randomizer") || force) {
        parts.append(QString("m_randomizer: %1").arg(m_randomizer));
    }
    if (settingsKeys.contains("differential") || force) {
        parts.append(QString("m_differential: %1").arg(m_differential));
    }
    if (settingsKeys.contains("convolutional") || force) {
        parts.append(QString("m_convolutional: %1").arg(m_convolutional));
    }
    if (settingsKeys.contains("packetFormat") || force) {
        parts.append(QString("m_packetFormat: %1").arg(m_packetFormat));
    }
    if (settingsKeys.contains("callsign") || force) {
        parts.append(QString("m_callsign: %1").arg(m_callsign));
    }
    if (settingsKeys.contains("spacecraftId") || force) {
        parts.append(QString("m_spacecraftId: %1").arg(m_spacecraftId));
    }
    if (settingsKeys.contains("virtualChannel") || force) {
        parts.append(QString("m_virtualChannel: %1").arg(m_virtualChannel));
    }
    if (settingsKeys.contains("imageFilename") || force) {
        parts.append(QString("m_imageFilename: %1").arg(m_imageFilename));
    }
    if (settingsKeys.contains("imageId") || force) {
        parts.append(QString("m_imageId: %1").arg(m_imageId));
    }
    if (settingsKeys.contains("quality") || force) {
        parts.append(QString("m_quality: %1").arg(m_quality));
    }
    if (settingsKeys.contains("width") || force) {
        parts.append(QString("m_width: %1").arg(m_width));
    }
    if (settingsKeys.contains("height") || force) {
        parts.append(QString("m_height: %1").arg(m_height));
    }
    if (settingsKeys.contains("fill") || force) {
        parts.append(QString("m_fill: %1").arg(m_fill));
    }
    if (settingsKeys.contains("preambleMs") || force) {
        parts.append(QString("m_preambleMs: %1").arg(m_preambleMs));
    }
    if (settingsKeys.contains("repeat") || force) {
        parts.append(QString("m_repeat: %1").arg(m_repeat));
    }
    if (settingsKeys.contains("repeatDelay") || force) {
        parts.append(QString("m_repeatDelay: %1").arg(m_repeatDelay));
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
