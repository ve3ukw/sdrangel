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

#include "sstvdemodwebapiadapter.h"

#include "SWGChannelSettings.h"
#include "SWGSSTVDemodSettings.h"
#include "sstvdemod.h"

int SSTVDemodWebAPIAdapter::webapiSettingsGet(
    SWGSDRangel::SWGChannelSettings& response,
    QString& errorMessage)
{
    (void) errorMessage;
    response.setSstvDemodSettings(new SWGSDRangel::SWGSSTVDemodSettings());
    response.getSstvDemodSettings()->init();
    SSTVDemod::webapiFormatChannelSettings(response, m_settings);
    return 200;
}

int SSTVDemodWebAPIAdapter::webapiSettingsPutPatch(
    bool force,
    const QStringList& channelSettingsKeys,
    SWGSDRangel::SWGChannelSettings& response,
    QString& errorMessage)
{
    (void) force;
    (void) errorMessage;
    SSTVDemod::webapiUpdateChannelSettings(m_settings, channelSettingsKeys, response);
    SSTVDemod::webapiFormatChannelSettings(response, m_settings);
    return 200;
}
