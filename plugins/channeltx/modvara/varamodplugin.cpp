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

#include "varamodplugin.h"

#include <QtPlugin>

#include "plugin/pluginapi.h"
#include "varamod.h"
#include "varamodwebapiadapter.h"
#ifndef SERVER_MODE
#include "varamodgui.h"
#endif

const PluginDescriptor VARAModPlugin::m_pluginDescriptor = {
    VARAMod::m_channelId,
    QStringLiteral("VARA Modulator"),
    QStringLiteral("7.28.0"),
    QStringLiteral("(c) Marcel, VE3UKW"),
    QStringLiteral("https://github.com/f4exb/sdrangel"),
    true,
    QStringLiteral("https://github.com/f4exb/sdrangel")
};

VARAModPlugin::VARAModPlugin(QObject *parent) :
    QObject(parent),
    m_pluginAPI(nullptr)
{}

const PluginDescriptor& VARAModPlugin::getPluginDescriptor() const
{
    return m_pluginDescriptor;
}

void VARAModPlugin::initPlugin(PluginAPI *pluginAPI)
{
    m_pluginAPI = pluginAPI;
    m_pluginAPI->registerTxChannel(
        VARAMod::m_channelIdURI,
        VARAMod::m_channelId,
        this);
}

void VARAModPlugin::createTxChannel(
    DeviceAPI *deviceAPI,
    BasebandSampleSource **basebandSource,
    ChannelAPI **channelAPI) const
{
    if (!basebandSource && !channelAPI) {
        return;
    }

    auto *instance = new VARAMod(deviceAPI);
    if (basebandSource) {
        *basebandSource = instance;
    }
    if (channelAPI) {
        *channelAPI = instance;
    }
}

ChannelGUI *VARAModPlugin::createTxChannelGUI(
    DeviceUISet *deviceUISet,
    BasebandSampleSource *txChannel) const
{
#ifdef SERVER_MODE
    (void) deviceUISet;
    (void) txChannel;
    return nullptr;
#else
    return VARAModGUI::create(m_pluginAPI, deviceUISet, txChannel);
#endif
}

ChannelWebAPIAdapter *VARAModPlugin::createChannelWebAPIAdapter() const
{
    return new VARAModWebAPIAdapter();
}
