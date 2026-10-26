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

#ifndef INCLUDE_ADDRESSBOOK_WEBAPIADAPTER_H
#define INCLUDE_ADDRESSBOOK_WEBAPIADAPTER_H

#include "feature/featurewebapiadapter.h"

#include "addressbooksettings.h"

class AddressBookWebAPIAdapter : public FeatureWebAPIAdapter {
public:
    AddressBookWebAPIAdapter() = default;
    ~AddressBookWebAPIAdapter() override = default;

    QByteArray serialize() const override { return m_settings.serialize(); }
    bool deserialize(const QByteArray& data) override { return m_settings.deserialize(data); }

private:
    AddressBookSettings m_settings;
};

#endif // INCLUDE_ADDRESSBOOK_WEBAPIADAPTER_H
