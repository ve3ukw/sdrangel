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

#include "ssdvdemodplugin.h"

#include <QtPlugin>

#include "plugin/pluginapi.h"
#include "ssdvdemod.h"
#include "ssdvdemodwebapiadapter.h"
#ifndef SERVER_MODE
#include "ssdvdemodgui.h"
#endif

const PluginDescriptor SSDVDemodPlugin::m_pluginDescriptor = {
    SSDVDemod::m_channelId,
    QStringLiteral("SSDV Demodulator"),
    QStringLiteral("8.0.0"),
    QStringLiteral("(c) Marcel, VE3UKW"),
    QStringLiteral("https://github.com/f4exb/sdrangel"),
    true,
    QStringLiteral("https://github.com/f4exb/sdrangel")
};

SSDVDemodPlugin::SSDVDemodPlugin(QObject *parent) :
    QObject(parent),
    m_pluginAPI(nullptr)
{}

const PluginDescriptor& SSDVDemodPlugin::getPluginDescriptor() const
{
    return m_pluginDescriptor;
}

void SSDVDemodPlugin::initPlugin(PluginAPI *pluginAPI)
{
    m_pluginAPI = pluginAPI;
    m_pluginAPI->registerRxChannel(
        SSDVDemod::m_channelIdURI,
        SSDVDemod::m_channelId,
        this);
}

void SSDVDemodPlugin::createRxChannel(
    DeviceAPI *deviceAPI,
    BasebandSampleSink **basebandSink,
    ChannelAPI **channelAPI) const
{
    if (!basebandSink && !channelAPI) {
        return;
    }

    auto *instance = new SSDVDemod(deviceAPI);
    if (basebandSink) {
        *basebandSink = instance;
    }
    if (channelAPI) {
        *channelAPI = instance;
    }
}

ChannelGUI *SSDVDemodPlugin::createRxChannelGUI(
    DeviceUISet *deviceUISet,
    BasebandSampleSink *rxChannel) const
{
#ifdef SERVER_MODE
    (void) deviceUISet;
    (void) rxChannel;
    return nullptr;
#else
    return SSDVDemodGUI::create(m_pluginAPI, deviceUISet, rxChannel);
#endif
}

ChannelWebAPIAdapter *SSDVDemodPlugin::createChannelWebAPIAdapter() const
{
    return new SSDVDemodWebAPIAdapter();
}
