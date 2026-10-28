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
#include "dsp/devicesamplemimo.h"
#include "dsp/ctcssfrequencies.h"
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

    // If a transmit channel (E.g. NFM modulator) or a channel of a different type is selected,
    // tune the receive channel of the entry's type that it is linked to instead
    if ((channel->getStreamType() != ChannelAPI::StreamSingleSink)
        || (!entry.m_channelType.isEmpty() && (channel->getIdentifier() != entry.m_channelType)))
    {
        if (ChannelAPI *linkedChannel = getLinkedRxChannel(entry, channel))
        {
            channel = linkedChannel;
            deviceSetIndex = channel->getDeviceSetIndex();
            channelIndex = channel->getIndexInDeviceSet();
        }
    }

    if (!entry.m_channelType.isEmpty() && (channel->getIdentifier() != entry.m_channelType))
    {
        errorMessage = QString("Entry needs a %1 channel, but %2 is a %3 channel")
            .arg(entry.m_channelType)
            .arg(MainCore::instance()->getChannelId(channel))
            .arg(channel->getIdentifier());
        return false;
    }

    if (channel->getStreamType() != ChannelAPI::StreamSingleSink)
    {
        errorMessage = "Only receive channels, or channels linked to one, can be tuned";
        return false;
    }

    qint64 centerFrequency;
    int sampleRate;
    int offset = 0;

    if (!getRxCenterFrequency(deviceSetIndex, channel, centerFrequency, sampleRate))
    {
        errorMessage = "Failed to get device center frequency";
        return false;
    }

    ChannelWebAPIUtils::getFrequencyOffset(deviceSetIndex, channelIndex, offset);

    // Only change the channel offset if the entry fits in the current baseband, otherwise move
    // the device center frequency and keep the channel's offset, so it stays clear of the DC spike
    qint64 newOffset = entry.m_frequency - centerFrequency;
    qint64 halfSpan = sampleRate / 2 - entry.m_bandwidth / 2;

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
        if (!setRxCenterFrequency(deviceSetIndex, channel, entry.m_frequency - offset))
        {
            errorMessage = "Failed to set device center frequency";
            return false;
        }
    }

    // Not all channels have an RF bandwidth setting, so a failure here isn't an error
    if ((entry.m_bandwidth > 0) && !ChannelWebAPIUtils::patchChannelSetting(deviceSetIndex, channelIndex, "rfBandwidth", entry.m_bandwidth)) {
        qDebug() << "AddressBook::tune: channel" << channelId << "has no rfBandwidth setting";
    }

    qint64 rxOffset = (std::abs(newOffset) <= halfSpan) ? newOffset : offset;
    int ctcssIndex = getCTCSSIndex(entry.m_ctcss);

    // CTCSS tone of the tuned channel (NFM demodulator), where index 0 is no tone. Ignored by channels without one
    ChannelWebAPIUtils::patchChannelSettings(deviceSetIndex, channelIndex, {{"ctcssIndex", ctcssIndex + 1}});

    // Channels linked to the tuned channel (E.g. NFM modulator of a transceiver) only follow changes made in its GUI,
    // so update them here, along with the repeater shift and CTCSS tone of the entry
    int link;

    if (ChannelWebAPIUtils::getChannelSetting(deviceSetIndex, channelIndex, "linkFrequencyOffset", link) && link) {
        tuneLinkedChannels(entry, channel, rxOffset, ctcssIndex);
    }

    return true;
}

// Get a receive channel of the entry's type (any type if the entry doesn't have one), that is linked to channel
ChannelAPI *AddressBook::getLinkedRxChannel(const AddressBookEntry& entry, const ChannelAPI *channel)
{
    int link;

    if (!ChannelWebAPIUtils::getChannelSetting(channel->getDeviceSetIndex(), channel->getIndexInDeviceSet(), "linkFrequencyOffset", link) || !link) {
        return nullptr;
    }

    QStringList linkGroupURIs = ChannelWebAPIUtils::getLinkGroupURIs(channel->getURI());

    for (ChannelAPI *linkedChannel : ChannelWebAPIUtils::getLinkedChannels(channel, linkGroupURIs))
    {
        if ((linkedChannel->getStreamType() == ChannelAPI::StreamSingleSink)
            && (entry.m_channelType.isEmpty() || (linkedChannel->getIdentifier() == entry.m_channelType))) {
            return linkedChannel;
        }
    }

    return nullptr;
}

// Get index of the CTCSS tone in CTCSSFrequencies, or -1 if there is none
int AddressBook::getCTCSSIndex(float ctcss)
{
    if (ctcss > 0.0f)
    {
        for (int i = 0; i < CTCSSFrequencies::m_nbFreqs; i++)
        {
            if (std::abs(CTCSSFrequencies::m_Freqs[i] - ctcss) < 0.05f) {
                return i;
            }
        }
    }

    return -1;
}

void AddressBook::tuneLinkedChannels(const AddressBookEntry& entry, const ChannelAPI *channel, qint64 rxOffset, int ctcssIndex)
{
    QStringList linkGroupURIs = ChannelWebAPIUtils::getLinkGroupURIs(channel->getURI());

    for (ChannelAPI *linkedChannel : ChannelWebAPIUtils::getLinkedChannels(channel, linkGroupURIs))
    {
        unsigned int linkedDeviceSetIndex = linkedChannel->getDeviceSetIndex();
        int linkedChannelIndex = linkedChannel->getIndexInDeviceSet();
        QVariantMap settings;
        int repeaterShift;

        if (linkedChannel->getStreamType() == ChannelAPI::StreamSingleSource)
        {
            settings["inputFrequencyOffset"] = rxOffset;

            if (ChannelWebAPIUtils::getChannelSetting(linkedDeviceSetIndex, linkedChannelIndex, "repeaterShift", repeaterShift))
            {
                settings["repeaterShift"] = entry.m_txOffset;

                // The repeater shift is applied to the Tx center frequency by the device when it can, else to the channel offset
                if (!ChannelWebAPIUtils::isTxFrequencyShiftLinked(linkedDeviceSetIndex)) {
                    settings["inputFrequencyOffset"] = rxOffset + entry.m_txOffset;
                }
            }

            // Modulator CTCSS index 0 is the first tone. A repeater needs the tone to be sent, so switch it on or off
            settings["ctcssOn"] = (ctcssIndex >= 0) ? 1 : 0;

            if (ctcssIndex >= 0) {
                settings["ctcssIndex"] = ctcssIndex;
            }
        }
        else
        {
            settings["inputFrequencyOffset"] = rxOffset;
            settings["ctcssIndex"] = ctcssIndex + 1;
        }

        ChannelWebAPIUtils::patchChannelSettings(linkedDeviceSetIndex, linkedChannelIndex, settings);
    }
}

bool AddressBook::getRxCenterFrequency(unsigned int deviceSetIndex, const ChannelAPI *channel, qint64& centerFrequency, int& sampleRate)
{
    std::vector<DeviceSet*>& deviceSets = MainCore::instance()->getDeviceSets();

    if ((deviceSetIndex >= deviceSets.size()) || (channel->getStreamType() != ChannelAPI::StreamSingleSink)) {
        return false;
    }

    DeviceAPI *deviceAPI = deviceSets[deviceSetIndex]->m_deviceAPI;

    if (DeviceSampleSource *source = deviceAPI->getSampleSource())
    {
        centerFrequency = source->getCenterFrequency();
        sampleRate = source->getSampleRate();
        return true;
    }
    else if (DeviceSampleMIMO *mimo = deviceAPI->getSampleMIMO())
    {
        centerFrequency = mimo->getSourceCenterFrequency(channel->getStreamIndex());
        sampleRate = mimo->getSourceSampleRate(channel->getStreamIndex());
        return true;
    }

    return false;
}

bool AddressBook::setRxCenterFrequency(unsigned int deviceSetIndex, const ChannelAPI *channel, qint64 centerFrequency)
{
    std::vector<DeviceSet*>& deviceSets = MainCore::instance()->getDeviceSets();

    if (deviceSetIndex >= deviceSets.size()) {
        return false;
    }

    DeviceAPI *deviceAPI = deviceSets[deviceSetIndex]->m_deviceAPI;

    if (deviceAPI->getSampleSource())
    {
        return ChannelWebAPIUtils::setCenterFrequency(deviceSetIndex, centerFrequency);
    }
    else if (DeviceSampleMIMO *mimo = deviceAPI->getSampleMIMO())
    {
        // Goes through the device's message queue, so device side links (E.g. PlutoSDR Rx to Tx) are applied
        mimo->setSourceCenterFrequency(centerFrequency, channel->getStreamIndex());
        return true;
    }

    return false;
}
