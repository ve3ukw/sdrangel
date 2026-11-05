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

#include "ssdvdemodsettings.h"

SSDVDemodSettings::SSDVDemodSettings() :
    m_channelMarker(nullptr),
    m_rollupState(nullptr),
    m_scopeGUI(nullptr)
{
    resetToDefaults();
}

void SSDVDemodSettings::resetToDefaults()
{
    m_inputFrequencyOffset = 0;
    m_baudRate = 9600;
    m_rrcRolloff = 0.35f;
    m_pllBandwidth = 0.02f;
    m_afc = true;
    m_frameSize = 223;
    m_rs = true;
    m_dualBasis = false;
    m_randomizer = true;
    m_differential = true;
    m_convolutional = true;
    m_packetFormat = AO123;
    m_autoSave = true;
    m_autoSavePath = "";
    m_saveFrames = false;
    m_framesFilename = "ssdv_frames.kss";
    m_rgbColor = QColor(0, 200, 255).rgb();
    m_title = "SSDV Demodulator";
    m_streamIndex = 0;
    m_useReverseAPI = false;
    m_reverseAPIAddress = "127.0.0.1";
    m_reverseAPIPort = 8888;
    m_reverseAPIDeviceIndex = 0;
    m_reverseAPIChannelIndex = 0;
    m_workspaceIndex = 0;
    m_hidden = false;
    m_modulation = BPSK;
    m_fskDeviation = 2400;
    m_framing = CCSDS;
}

QByteArray SSDVDemodSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeS32(1, m_inputFrequencyOffset);
    s.writeS32(2, m_baudRate);
    s.writeReal(3, m_rrcRolloff);
    s.writeReal(4, m_pllBandwidth);
    s.writeBool(5, m_afc);
    s.writeS32(6, m_frameSize);
    s.writeBool(7, m_rs);
    s.writeBool(8, m_dualBasis);
    s.writeBool(9, m_randomizer);
    s.writeBool(10, m_differential);
    s.writeBool(11, m_convolutional);
    s.writeS32(12, m_packetFormat);
    s.writeBool(13, m_autoSave);
    s.writeString(14, m_autoSavePath);
    s.writeBool(15, m_saveFrames);
    s.writeString(16, m_framesFilename);
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
    if (m_scopeGUI) {
        s.writeBlob(30, m_scopeGUI->serialize());
    }

    s.writeS32(31, m_modulation);
    s.writeS32(32, m_fskDeviation);
    s.writeS32(33, m_framing);

    return s.final();
}

bool SSDVDemodSettings::deserialize(const QByteArray& data)
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
        d.readS32(2, &m_baudRate, 9600);
        d.readReal(3, &m_rrcRolloff, 0.35f);
        d.readReal(4, &m_pllBandwidth, 0.02f);
        d.readBool(5, &m_afc, true);
        d.readS32(6, &m_frameSize, 223);
        d.readBool(7, &m_rs, true);
        d.readBool(8, &m_dualBasis, false);
        d.readBool(9, &m_randomizer, true);
        d.readBool(10, &m_differential, true);
        d.readBool(11, &m_convolutional, true);
        d.readS32(12, &m_packetFormat, AO123);
        d.readBool(13, &m_autoSave, true);
        d.readString(14, &m_autoSavePath, "");
        d.readBool(15, &m_saveFrames, false);
        d.readString(16, &m_framesFilename, "ssdv_frames.kss");
        d.readU32(17, &m_rgbColor, QColor(0, 200, 255).rgb());
        d.readString(18, &m_title, "SSDV Demodulator");
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
        if (m_scopeGUI)
        {
            d.readBlob(30, &bytetmp);
            m_scopeGUI->deserialize(bytetmp);
        }

        d.readS32(31, &m_modulation, BPSK);
        d.readS32(32, &m_fskDeviation, 2400);
        d.readS32(33, &m_framing, CCSDS);

        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

void SSDVDemodSettings::applySettings(const QStringList& settingsKeys, const SSDVDemodSettings& settings)
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
    if (settingsKeys.contains("pllBandwidth")) {
        m_pllBandwidth = settings.m_pllBandwidth;
    }
    if (settingsKeys.contains("afc")) {
        m_afc = settings.m_afc;
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
    if (settingsKeys.contains("autoSave")) {
        m_autoSave = settings.m_autoSave;
    }
    if (settingsKeys.contains("autoSavePath")) {
        m_autoSavePath = settings.m_autoSavePath;
    }
    if (settingsKeys.contains("saveFrames")) {
        m_saveFrames = settings.m_saveFrames;
    }
    if (settingsKeys.contains("framesFilename")) {
        m_framesFilename = settings.m_framesFilename;
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
    if (settingsKeys.contains("modulation")) {
        m_modulation = settings.m_modulation;
    }
    if (settingsKeys.contains("fskDeviation")) {
        m_fskDeviation = settings.m_fskDeviation;
    }
    if (settingsKeys.contains("framing")) {
        m_framing = settings.m_framing;
    }
}

QString SSDVDemodSettings::getDebugString(const QStringList& settingsKeys, bool force) const
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
    if (settingsKeys.contains("pllBandwidth") || force) {
        parts.append(QString("m_pllBandwidth: %1").arg(m_pllBandwidth));
    }
    if (settingsKeys.contains("afc") || force) {
        parts.append(QString("m_afc: %1").arg(m_afc));
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
    if (settingsKeys.contains("autoSave") || force) {
        parts.append(QString("m_autoSave: %1").arg(m_autoSave));
    }
    if (settingsKeys.contains("autoSavePath") || force) {
        parts.append(QString("m_autoSavePath: %1").arg(m_autoSavePath));
    }
    if (settingsKeys.contains("saveFrames") || force) {
        parts.append(QString("m_saveFrames: %1").arg(m_saveFrames));
    }
    if (settingsKeys.contains("framesFilename") || force) {
        parts.append(QString("m_framesFilename: %1").arg(m_framesFilename));
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
    if (settingsKeys.contains("modulation") || force) {
        parts.append(QString("m_modulation: %1").arg(m_modulation));
    }
    if (settingsKeys.contains("fskDeviation") || force) {
        parts.append(QString("m_fskDeviation: %1").arg(m_fskDeviation));
    }
    if (settingsKeys.contains("framing") || force) {
        parts.append(QString("m_framing: %1").arg(m_framing));
    }

    return " " + parts.join(" ");
}
