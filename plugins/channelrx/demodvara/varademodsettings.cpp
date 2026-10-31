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

#include "varaengine.h"
#include "varademodsettings.h"

VARADemodSettings::VARADemodSettings() :
    m_channelMarker(nullptr),
    m_rollupState(nullptr)
{
    resetToDefaults();
}

void VARADemodSettings::resetToDefaults()
{
    m_inputFrequencyOffset = 0;
    m_mode = VARAEngine::FMNarrow;
    m_callsign = "";
    m_address = "127.0.0.1";
    m_cmdPort = 8300;
    m_kissEnabled = false;
    m_kissPort = 8100;
    m_driveDb = -5.0f;
    m_rxLagMs = 0;
    m_fmDeviation = 3000.0f;
    m_agc = true;
    m_rxGainDb = 0.0f;
    m_volume = 1.0f;
    m_audioMute = true;
    m_audioDeviceName = AudioDeviceManager::m_defaultDeviceName;
    m_linkFrequencyOffset = false;
    m_rgbColor = QColor(255, 170, 0).rgb();
    m_title = "VARA Demodulator";
    m_streamIndex = 0;
    m_useReverseAPI = false;
    m_reverseAPIAddress = "127.0.0.1";
    m_reverseAPIPort = 8888;
    m_reverseAPIDeviceIndex = 0;
    m_reverseAPIChannelIndex = 0;
    m_workspaceIndex = 0;
    m_hidden = false;
}

QByteArray VARADemodSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeS32(1, m_inputFrequencyOffset);
    s.writeS32(2, m_mode);
    s.writeString(3, m_callsign);
    s.writeString(4, m_address);
    s.writeS32(5, m_cmdPort);
    s.writeBool(6, m_kissEnabled);
    s.writeS32(7, m_kissPort);
    s.writeReal(8, m_driveDb);
    s.writeS32(9, m_rxLagMs);
    s.writeReal(10, m_fmDeviation);
    s.writeBool(11, m_agc);
    s.writeReal(12, m_rxGainDb);
    s.writeReal(13, m_volume);
    s.writeBool(14, m_audioMute);
    s.writeString(15, m_audioDeviceName);
    s.writeBool(16, m_linkFrequencyOffset);
    s.writeU32(17, m_rgbColor);
    s.writeString(18, m_title);
    if (m_channelMarker) {
        s.writeBlob(19, m_channelMarker->serialize());
    }

    s.writeS32(20, m_streamIndex);
    s.writeBool(21, m_useReverseAPI);
    s.writeString(22, m_reverseAPIAddress);
    s.writeU32(23, m_reverseAPIPort);
    s.writeU32(24, m_reverseAPIDeviceIndex);
    s.writeU32(25, m_reverseAPIChannelIndex);
    if (m_rollupState) {
        s.writeBlob(26, m_rollupState->serialize());
    }

    s.writeS32(27, m_workspaceIndex);
    s.writeBlob(28, m_geometryBytes);
    s.writeBool(29, m_hidden);

    return s.final();
}

bool VARADemodSettings::deserialize(const QByteArray& data)
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
        d.readS32(2, &m_mode, VARAEngine::FMNarrow);
        d.readString(3, &m_callsign, "");
        d.readString(4, &m_address, "127.0.0.1");
        d.readS32(5, &m_cmdPort, 8300);
        d.readBool(6, &m_kissEnabled, false);
        d.readS32(7, &m_kissPort, 8100);
        d.readReal(8, &m_driveDb, -5.0f);
        d.readS32(9, &m_rxLagMs, 0);
        d.readReal(10, &m_fmDeviation, 3000.0f);
        d.readBool(11, &m_agc, true);
        d.readReal(12, &m_rxGainDb, 0.0f);
        d.readReal(13, &m_volume, 1.0f);
        d.readBool(14, &m_audioMute, true);
        d.readString(15, &m_audioDeviceName, AudioDeviceManager::m_defaultDeviceName);
        d.readBool(16, &m_linkFrequencyOffset, false);
        d.readU32(17, &m_rgbColor, QColor(255, 170, 0).rgb());
        d.readString(18, &m_title, "VARA Demodulator");
        if (m_channelMarker)
        {
            d.readBlob(19, &bytetmp);
            m_channelMarker->deserialize(bytetmp);
        }

        d.readS32(20, &m_streamIndex, 0);
        d.readBool(21, &m_useReverseAPI, false);
        d.readString(22, &m_reverseAPIAddress, "127.0.0.1");
        d.readU32(23, &utmp, 0);

        if ((utmp > 1023) && (utmp < 65535)) {
            m_reverseAPIPort = utmp;
        } else {
            m_reverseAPIPort = 8888;
        }

        d.readU32(24, &utmp, 0);
        m_reverseAPIDeviceIndex = utmp > 99 ? 99 : utmp;
        d.readU32(25, &utmp, 0);
        m_reverseAPIChannelIndex = utmp > 99 ? 99 : utmp;
        if (m_rollupState)
        {
            d.readBlob(26, &bytetmp);
            m_rollupState->deserialize(bytetmp);
        }

        d.readS32(27, &m_workspaceIndex, 0);
        d.readBlob(28, &m_geometryBytes);
        d.readBool(29, &m_hidden, false);

        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

void VARADemodSettings::applySettings(const QStringList& settingsKeys, const VARADemodSettings& settings)
{
    if (settingsKeys.contains("inputFrequencyOffset")) {
        m_inputFrequencyOffset = settings.m_inputFrequencyOffset;
    }
    if (settingsKeys.contains("mode")) {
        m_mode = settings.m_mode;
    }
    if (settingsKeys.contains("callsign")) {
        m_callsign = settings.m_callsign;
    }
    if (settingsKeys.contains("address")) {
        m_address = settings.m_address;
    }
    if (settingsKeys.contains("cmdPort")) {
        m_cmdPort = settings.m_cmdPort;
    }
    if (settingsKeys.contains("kissEnabled")) {
        m_kissEnabled = settings.m_kissEnabled;
    }
    if (settingsKeys.contains("kissPort")) {
        m_kissPort = settings.m_kissPort;
    }
    if (settingsKeys.contains("driveDb")) {
        m_driveDb = settings.m_driveDb;
    }
    if (settingsKeys.contains("rxLagMs")) {
        m_rxLagMs = settings.m_rxLagMs;
    }
    if (settingsKeys.contains("fmDeviation")) {
        m_fmDeviation = settings.m_fmDeviation;
    }
    if (settingsKeys.contains("agc")) {
        m_agc = settings.m_agc;
    }
    if (settingsKeys.contains("rxGainDb")) {
        m_rxGainDb = settings.m_rxGainDb;
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

QString VARADemodSettings::getDebugString(const QStringList& settingsKeys, bool force) const
{
    QStringList parts;

    if (settingsKeys.contains("inputFrequencyOffset") || force) {
        parts.append(QString("m_inputFrequencyOffset: %1").arg(m_inputFrequencyOffset));
    }
    if (settingsKeys.contains("mode") || force) {
        parts.append(QString("m_mode: %1").arg(m_mode));
    }
    if (settingsKeys.contains("callsign") || force) {
        parts.append(QString("m_callsign: %1").arg(m_callsign));
    }
    if (settingsKeys.contains("address") || force) {
        parts.append(QString("m_address: %1").arg(m_address));
    }
    if (settingsKeys.contains("cmdPort") || force) {
        parts.append(QString("m_cmdPort: %1").arg(m_cmdPort));
    }
    if (settingsKeys.contains("kissEnabled") || force) {
        parts.append(QString("m_kissEnabled: %1").arg(m_kissEnabled));
    }
    if (settingsKeys.contains("kissPort") || force) {
        parts.append(QString("m_kissPort: %1").arg(m_kissPort));
    }
    if (settingsKeys.contains("driveDb") || force) {
        parts.append(QString("m_driveDb: %1").arg(m_driveDb));
    }
    if (settingsKeys.contains("rxLagMs") || force) {
        parts.append(QString("m_rxLagMs: %1").arg(m_rxLagMs));
    }
    if (settingsKeys.contains("fmDeviation") || force) {
        parts.append(QString("m_fmDeviation: %1").arg(m_fmDeviation));
    }
    if (settingsKeys.contains("agc") || force) {
        parts.append(QString("m_agc: %1").arg(m_agc));
    }
    if (settingsKeys.contains("rxGainDb") || force) {
        parts.append(QString("m_rxGainDb: %1").arg(m_rxGainDb));
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
