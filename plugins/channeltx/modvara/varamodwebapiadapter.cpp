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

#include "varamodwebapiadapter.h"

#include "SWGChannelSettings.h"
#include "SWGVARAModSettings.h"
#include "varamod.h"

int VARAModWebAPIAdapter::webapiSettingsGet(
    SWGSDRangel::SWGChannelSettings& response,
    QString& errorMessage)
{
    (void) errorMessage;
    response.setVaraModSettings(new SWGSDRangel::SWGVARAModSettings());
    response.getVaraModSettings()->init();
    VARAMod::webapiFormatChannelSettings(response, m_settings);
    return 200;
}

int VARAModWebAPIAdapter::webapiSettingsPutPatch(
    bool force,
    const QStringList& channelSettingsKeys,
    SWGSDRangel::SWGChannelSettings& response,
    QString& errorMessage)
{
    (void) force;
    (void) errorMessage;
    VARAMod::webapiUpdateChannelSettings(m_settings, channelSettingsKeys, response);
    VARAMod::webapiFormatChannelSettings(response, m_settings);
    return 200;
}
