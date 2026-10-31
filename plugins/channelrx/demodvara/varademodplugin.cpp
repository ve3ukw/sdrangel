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

#include "varademodplugin.h"

#include <QtPlugin>

#include "plugin/pluginapi.h"
#include "varademod.h"
#include "varademodwebapiadapter.h"
#ifndef SERVER_MODE
#include "varademodgui.h"
#endif

const PluginDescriptor VARADemodPlugin::m_pluginDescriptor = {
    VARADemod::m_channelId,
    QStringLiteral("VARA Demodulator"),
    QStringLiteral("7.28.0"),
    QStringLiteral("(c) Marcel, VE3UKW"),
    QStringLiteral("https://github.com/f4exb/sdrangel"),
    true,
    QStringLiteral("https://github.com/f4exb/sdrangel")
};

VARADemodPlugin::VARADemodPlugin(QObject *parent) :
    QObject(parent),
    m_pluginAPI(nullptr)
{}

const PluginDescriptor& VARADemodPlugin::getPluginDescriptor() const
{
    return m_pluginDescriptor;
}

void VARADemodPlugin::initPlugin(PluginAPI *pluginAPI)
{
    m_pluginAPI = pluginAPI;
    m_pluginAPI->registerRxChannel(
        VARADemod::m_channelIdURI,
        VARADemod::m_channelId,
        this);
}

void VARADemodPlugin::createRxChannel(
    DeviceAPI *deviceAPI,
    BasebandSampleSink **basebandSink,
    ChannelAPI **channelAPI) const
{
    if (!basebandSink && !channelAPI) {
        return;
    }

    auto *instance = new VARADemod(deviceAPI);
    if (basebandSink) {
        *basebandSink = instance;
    }
    if (channelAPI) {
        *channelAPI = instance;
    }
}

ChannelGUI *VARADemodPlugin::createRxChannelGUI(
    DeviceUISet *deviceUISet,
    BasebandSampleSink *rxChannel) const
{
#ifdef SERVER_MODE
    (void) deviceUISet;
    (void) rxChannel;
    return nullptr;
#else
    return VARADemodGUI::create(m_pluginAPI, deviceUISet, rxChannel);
#endif
}

ChannelWebAPIAdapter *VARADemodPlugin::createChannelWebAPIAdapter() const
{
    return new VARADemodWebAPIAdapter();
}
