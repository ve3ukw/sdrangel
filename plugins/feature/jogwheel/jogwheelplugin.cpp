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

#include <QtPlugin>

#include "plugin/pluginapi.h"

#ifndef SERVER_MODE
#include "jogwheelgui.h"
#endif

#include "jogwheel.h"
#include "jogwheelplugin.h"
#include "jogwheelwebapiadapter.h"

const PluginDescriptor JogWheelPlugin::m_pluginDescriptor = {
    JogWheel::m_featureId,
    QStringLiteral("Jog Wheel"),
    QStringLiteral("0.1.0"),
    QStringLiteral("(c) Marcel, VE3UKW"),
    QStringLiteral("https://github.com/f4exb/sdrangel"),
    true,
    QStringLiteral("https://github.com/f4exb/sdrangel")
};

JogWheelPlugin::JogWheelPlugin(QObject* parent) :
    QObject(parent),
    m_pluginAPI(nullptr)
{
}

const PluginDescriptor& JogWheelPlugin::getPluginDescriptor() const
{
    return m_pluginDescriptor;
}

void JogWheelPlugin::initPlugin(PluginAPI* pluginAPI)
{
    m_pluginAPI = pluginAPI;
    m_pluginAPI->registerFeature(JogWheel::m_featureIdURI, JogWheel::m_featureId, this);
}

#ifdef SERVER_MODE
FeatureGUI* JogWheelPlugin::createFeatureGUI(FeatureUISet *featureUISet, Feature *feature) const
{
    (void) featureUISet;
    (void) feature;
    return nullptr;
}
#else
FeatureGUI* JogWheelPlugin::createFeatureGUI(FeatureUISet *featureUISet, Feature *feature) const
{
    return JogWheelGUI::create(m_pluginAPI, featureUISet, feature);
}
#endif

Feature* JogWheelPlugin::createFeature(WebAPIAdapterInterface* webAPIAdapterInterface) const
{
    return new JogWheel(webAPIAdapterInterface);
}

FeatureWebAPIAdapter* JogWheelPlugin::createFeatureWebAPIAdapter() const
{
    return new JogWheelWebAPIAdapter();
}
