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
#include "addressbookgui.h"
#endif

#include "addressbook.h"
#include "addressbookplugin.h"
#include "addressbookwebapiadapter.h"

const PluginDescriptor AddressBookPlugin::m_pluginDescriptor = {
    AddressBook::m_featureId,
    QStringLiteral("Address Book"),
    QStringLiteral("7.27.2"),
    QStringLiteral("(c) Marcel, VE3UKW"),
    QStringLiteral("https://github.com/f4exb/sdrangel"),
    true,
    QStringLiteral("https://github.com/f4exb/sdrangel")
};

AddressBookPlugin::AddressBookPlugin(QObject* parent) :
    QObject(parent),
    m_pluginAPI(nullptr)
{
}

const PluginDescriptor& AddressBookPlugin::getPluginDescriptor() const
{
    return m_pluginDescriptor;
}

void AddressBookPlugin::initPlugin(PluginAPI* pluginAPI)
{
    m_pluginAPI = pluginAPI;
    m_pluginAPI->registerFeature(AddressBook::m_featureIdURI, AddressBook::m_featureId, this);
}

#ifdef SERVER_MODE
FeatureGUI* AddressBookPlugin::createFeatureGUI(FeatureUISet *featureUISet, Feature *feature) const
{
    (void) featureUISet;
    (void) feature;
    return nullptr;
}
#else
FeatureGUI* AddressBookPlugin::createFeatureGUI(FeatureUISet *featureUISet, Feature *feature) const
{
    return AddressBookGUI::create(m_pluginAPI, featureUISet, feature);
}
#endif

Feature* AddressBookPlugin::createFeature(WebAPIAdapterInterface* webAPIAdapterInterface) const
{
    return new AddressBook(webAPIAdapterInterface);
}

FeatureWebAPIAdapter* AddressBookPlugin::createFeatureWebAPIAdapter() const
{
    return new AddressBookWebAPIAdapter();
}
