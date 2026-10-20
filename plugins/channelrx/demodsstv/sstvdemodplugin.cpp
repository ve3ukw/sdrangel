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

#include "sstvdemodplugin.h"

#include <QtPlugin>

#include "plugin/pluginapi.h"
#include "sstvdemod.h"
#include "sstvdemodwebapiadapter.h"
#ifndef SERVER_MODE
#include "sstvdemodgui.h"
#endif

const PluginDescriptor SSTVDemodPlugin::m_pluginDescriptor = {
    SSTVDemod::m_channelId,
    QStringLiteral("SSTV Demodulator"),
    QStringLiteral("7.28.0"),
    QStringLiteral("(c) Marcel, VE3UKW"),
    QStringLiteral("https://github.com/f4exb/sdrangel"),
    true,
    QStringLiteral("https://github.com/f4exb/sdrangel")
};

SSTVDemodPlugin::SSTVDemodPlugin(QObject *parent) :
    QObject(parent),
    m_pluginAPI(nullptr)
{}

const PluginDescriptor& SSTVDemodPlugin::getPluginDescriptor() const
{
    return m_pluginDescriptor;
}

void SSTVDemodPlugin::initPlugin(PluginAPI *pluginAPI)
{
    m_pluginAPI = pluginAPI;
    m_pluginAPI->registerRxChannel(
        SSTVDemod::m_channelIdURI,
        SSTVDemod::m_channelId,
        this);
}

void SSTVDemodPlugin::createRxChannel(
    DeviceAPI *deviceAPI,
    BasebandSampleSink **basebandSink,
    ChannelAPI **channelAPI) const
{
    if (!basebandSink && !channelAPI) {
        return;
    }

    auto *instance = new SSTVDemod(deviceAPI);
    if (basebandSink) {
        *basebandSink = instance;
    }
    if (channelAPI) {
        *channelAPI = instance;
    }
}

ChannelGUI *SSTVDemodPlugin::createRxChannelGUI(
    DeviceUISet *deviceUISet,
    BasebandSampleSink *rxChannel) const
{
#ifdef SERVER_MODE
    (void) deviceUISet;
    (void) rxChannel;
    return nullptr;
#else
    return SSTVDemodGUI::create(m_pluginAPI, deviceUISet, rxChannel);
#endif
}

ChannelWebAPIAdapter *SSTVDemodPlugin::createChannelWebAPIAdapter() const
{
    return new SSTVDemodWebAPIAdapter();
}
