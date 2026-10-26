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

#include <QDebug>

#include "channel/channelapi.h"
#include "channel/channelwebapiutils.h"
#include "device/deviceapi.h"
#include "device/deviceset.h"
#include "dsp/devicesamplesource.h"
#include "maincore.h"

#include "addressbook.h"

MESSAGE_CLASS_DEFINITION(AddressBook::MsgConfigureAddressBook, Message)
MESSAGE_CLASS_DEFINITION(AddressBook::MsgTune, Message)
MESSAGE_CLASS_DEFINITION(AddressBook::MsgTuneResult, Message)

const char* const AddressBook::m_featureIdURI = "sdrangel.feature.addressbook";
const char* const AddressBook::m_featureId = "AddressBook";

AddressBook::AddressBook(WebAPIAdapterInterface *webAPIAdapterInterface) :
    Feature(m_featureIdURI, webAPIAdapterInterface)
{
    qDebug("AddressBook::AddressBook: webAPIAdapterInterface: %p", webAPIAdapterInterface);
    setObjectName(m_featureId);
    setState(StIdle);
}

bool AddressBook::handleMessage(const Message& cmd)
{
    if (MsgConfigureAddressBook::match(cmd))
    {
        const MsgConfigureAddressBook& cfg = (const MsgConfigureAddressBook&) cmd;
        qDebug() << "AddressBook::handleMessage: MsgConfigureAddressBook";
        applySettings(cfg.getSettings(), cfg.getSettingsKeys(), cfg.getForce());
        return true;
    }
    else if (MsgTune::match(cmd))
    {
        const MsgTune& tuneMsg = (const MsgTune&) cmd;
        QString errorMessage;
        bool success = tune(tuneMsg.getEntry(), tuneMsg.getChannelId(), errorMessage);

        if (m_guiMessageQueue) {
            m_guiMessageQueue->push(MsgTuneResult::create(success, errorMessage));
        }

        return true;
    }

    return false;
}

QByteArray AddressBook::serialize() const
{
    return m_settings.serialize();
}

bool AddressBook::deserialize(const QByteArray& data)
{
    if (m_settings.deserialize(data))
    {
        MsgConfigureAddressBook *msg = MsgConfigureAddressBook::create(m_settings, QList<QString>(), true);
        m_inputMessageQueue.push(msg);
        return true;
    }
    else
    {
        m_settings.resetToDefaults();
        MsgConfigureAddressBook *msg = MsgConfigureAddressBook::create(m_settings, QList<QString>(), true);
        m_inputMessageQueue.push(msg);
        return false;
    }
}

void AddressBook::applySettings(const AddressBookSettings& settings, const QStringList& settingsKeys, bool force)
{
    qDebug() << "AddressBook::applySettings:" << settingsKeys << " force: " << force;

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }
}

bool AddressBook::tune(const AddressBookEntry& entry, const QString& channelId, QString& errorMessage)
{
    unsigned int deviceSetIndex;
    unsigned int channelIndex;

    if (!MainCore::getDeviceAndChannelIndexFromId(channelId, deviceSetIndex, channelIndex))
    {
        errorMessage = "No channel selected";
        return false;
    }

    ChannelAPI *channel = MainCore::instance()->getChannel(deviceSetIndex, channelIndex);

    if (!channel)
    {
        errorMessage = QString("Channel %1 not found").arg(channelId);
        return false;
    }

    if (!entry.m_channelType.isEmpty() && (channel->getIdentifier() != entry.m_channelType))
    {
        errorMessage = QString("Entry needs a %1 channel, but %2 is %3")
            .arg(entry.m_channelType)
            .arg(channelId)
            .arg(channel->getIdentifier());
        return false;
    }

    std::vector<DeviceSet*>& deviceSets = MainCore::instance()->getDeviceSets();
    DeviceSampleSource *source = (deviceSetIndex < deviceSets.size()) ? deviceSets[deviceSetIndex]->m_deviceAPI->getSampleSource() : nullptr;

    if (!source)
    {
        errorMessage = "Only receive channels can be tuned";
        return false;
    }

    double centerFrequency;
    int offset = 0;

    if (!ChannelWebAPIUtils::getCenterFrequency(deviceSetIndex, centerFrequency))
    {
        errorMessage = "Failed to get device center frequency";
        return false;
    }

    ChannelWebAPIUtils::getFrequencyOffset(deviceSetIndex, channelIndex, offset);

    // Only change the channel offset if the entry fits in the current baseband, otherwise move
    // the device center frequency and keep the channel's offset, so it stays clear of the DC spike
    qint64 newOffset = entry.m_frequency - qRound64(centerFrequency);
    qint64 halfSpan = source->getSampleRate() / 2 - entry.m_bandwidth / 2;

    if (std::abs(newOffset) <= halfSpan)
    {
        if (!ChannelWebAPIUtils::setFrequencyOffset(deviceSetIndex, channelIndex, newOffset))
        {
            errorMessage = "Failed to set channel frequency offset";
            return false;
        }
    }
    else
    {
        if (!ChannelWebAPIUtils::setCenterFrequency(deviceSetIndex, entry.m_frequency - offset))
        {
            errorMessage = "Failed to set device center frequency";
            return false;
        }
    }

    // Not all channels have an RF bandwidth setting, so a failure here isn't an error
    if ((entry.m_bandwidth > 0) && !ChannelWebAPIUtils::patchChannelSetting(deviceSetIndex, channelIndex, "rfBandwidth", entry.m_bandwidth)) {
        qDebug() << "AddressBook::tune: channel" << channelId << "has no rfBandwidth setting";
    }

    return true;
}
