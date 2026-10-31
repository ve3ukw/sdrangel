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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QBuffer>
#include <QThread>

#include "SWGChannelSettings.h"
#include "SWGWorkspaceInfo.h"
#include "SWGVARAModSettings.h"
#include "SWGChannelReport.h"
#include "SWGVARAModReport.h"

#include "dsp/dspcommands.h"
#include "device/deviceapi.h"
#include "settings/serializable.h"
#include "util/db.h"
#include "util/udpptt.h"
#include "maincore.h"

#include "varaengine.h"
#include "varatxlink.h"
#include "varamodbaseband.h"
#include "varamod.h"

MESSAGE_CLASS_DEFINITION(VARAMod::MsgConfigureVARAMod, Message)
MESSAGE_CLASS_DEFINITION(VARAMod::MsgQueryPTT, Message)

const char* const VARAMod::m_channelIdURI = "sdrangel.channeltx.modvara";
const char* const VARAMod::m_channelId = "VARAMod";

VARAMod::VARAMod(DeviceAPI *deviceAPI) :
    ChannelAPI(m_channelIdURI, ChannelAPI::StreamSingleSource),
    m_deviceAPI(deviceAPI),
    m_thread(nullptr),
    m_running(false),
    m_basebandSource(nullptr),
    m_basebandSampleRate(0),
    m_centerFrequency(0),
    m_txLinkPort(0),
    m_ptt(new UDPPTT()),
    m_pttEnabled(new std::atomic<bool>(false)),
    m_levelMeter(nullptr)
{
    setObjectName(m_channelId);
    QObject::connect(m_ptt.get(), &UDPPTT::replyReceived, this, &VARAMod::pttReplyReceived);
    QObject::connect(m_ptt.get(), &UDPPTT::error, this, &VARAMod::pttReplyReceived);

    applySettings(QStringList(), m_settings, true);

    m_deviceAPI->addChannelSource(this);
    m_deviceAPI->addChannelSourceAPI(this);

    m_networkManager = new QNetworkAccessManager();
    QObject::connect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &VARAMod::networkManagerFinished
    );
}

VARAMod::~VARAMod()
{
    QObject::disconnect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &VARAMod::networkManagerFinished
    );
    delete m_networkManager;
    m_deviceAPI->removeChannelSourceAPI(this);
    m_deviceAPI->removeChannelSource(this, true);
    stop();
    m_ptt->setPtt(false);
    QObject::disconnect(m_ptt.get(), nullptr, this, nullptr);
}

void VARAMod::setDeviceAPI(DeviceAPI *deviceAPI)
{
    if (deviceAPI != m_deviceAPI)
    {
        m_deviceAPI->removeChannelSourceAPI(this);
        m_deviceAPI->removeChannelSource(this, false);
        m_deviceAPI = deviceAPI;
        m_deviceAPI->addChannelSource(this);
        m_deviceAPI->addChannelSourceAPI(this);
    }
}

void VARAMod::start()
{
    if (m_running) {
        return;
    }

    qDebug("VARAMod::start");
    m_thread = new QThread(this);
    m_thread->setObjectName("VARAMod");
    m_basebandSource = new VARAModBaseband();
    m_basebandSource->setChannel(this);
    m_basebandSource->reset();
    m_basebandSource->moveToThread(m_thread);

    QObject::connect(
        m_thread,
        &QThread::finished,
        m_basebandSource,
        &QObject::deleteLater
    );
    QObject::connect(
        m_thread,
        &QThread::finished,
        m_thread,
        &QThread::deleteLater
    );

    m_thread->start();

    if (m_levelMeter) {
        connect(m_basebandSource, SIGNAL(levelChanged(qreal, qreal, int)), m_levelMeter, SLOT(levelChanged(qreal, qreal, int)));
    }

    DSPSignalNotification *dspMsg = new DSPSignalNotification(m_basebandSampleRate, m_centerFrequency);
    m_basebandSource->getInputMessageQueue()->push(dspMsg);

    VARAModBaseband::MsgConfigureVARAModBaseband *msg = VARAModBaseband::MsgConfigureVARAModBaseband::create(QStringList(), m_settings, true);
    m_basebandSource->getInputMessageQueue()->push(msg);

    m_running = true;
    updateTxLink();
}

void VARAMod::stop()
{
    if (!m_running) {
        return;
    }

    qDebug("VARAMod::stop");
    m_running = false;
    updateTxLink();
    m_thread->quit();
    m_thread->wait();
    m_basebandSource = nullptr; // Freed by deleteLater when the thread finished
}

void VARAMod::updateTxLink()
{
    bool wanted = m_running && !m_settings.m_channelMute;

    if (m_txLink && (!wanted || (m_txLinkPort != m_settings.m_cmdPort)))
    {
        // Dropping the link while keyed would leave the transmitter keyed until the sequencer's heartbeat timeout
        m_txLink->setPttCallback(nullptr);
        m_txLink.reset();
        m_ptt->setPtt(false);

        if (m_basebandSource) {
            m_basebandSource->setTxLink(nullptr);
        }

        qDebug("VARAMod::updateTxLink: detached from port %d", m_txLinkPort);
    }

    if (wanted && !m_txLink)
    {
        m_txLink = VARATxLink::create(m_settings.m_cmdPort);
        m_txLinkPort = m_settings.m_cmdPort;
        std::shared_ptr<UDPPTT> ptt = m_ptt;
        std::shared_ptr<std::atomic<bool>> enabled = m_pttEnabled;
        m_txLink->setPttCallback([ptt, enabled](bool keyed) {
            ptt->setPtt(keyed && *enabled);
        });
        applyTxTiming();
        m_basebandSource->setTxLink(m_txLink);
        qDebug("VARAMod::updateTxLink: attached to port %d", m_txLinkPort);
    }
}

void VARAMod::applyTxTiming()
{
    if (m_txLink)
    {
        const int samplesPerMs = VARAEngine::m_sampleRate / 1000;
        m_txLink->setTiming(
            m_settings.m_pttLeadMs * samplesPerMs,
            m_fifoWindowMs * samplesPerMs,
            m_settings.m_txPathMs * samplesPerMs);
    }
}

void VARAMod::pull(SampleVector::iterator& begin, unsigned int nbSamples)
{
    if (m_running) {
        m_basebandSource->pull(begin, nbSamples);
    }
}

void VARAMod::setCenterFrequency(qint64 frequency)
{
    VARAModSettings settings = m_settings;
    settings.m_inputFrequencyOffset = frequency;
    applySettings(QStringList("inputFrequencyOffset"), settings, false);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureVARAMod *msgToGUI = MsgConfigureVARAMod::create(QStringList("inputFrequencyOffset"), settings, false);
        m_guiMessageQueue->push(msgToGUI);
    }
}

bool VARAMod::handleMessage(const Message& cmd)
{
    if (MsgConfigureVARAMod::match(cmd))
    {
        auto& cfg = (const MsgConfigureVARAMod&) cmd;
        qDebug() << "VARAMod::handleMessage: MsgConfigureVARAMod";
        applySettings(cfg.getSettingsKeys(), cfg.getSettings(), cfg.getForce());

        return true;
    }
    else if (MsgQueryPTT::match(cmd))
    {
        m_ptt->requestStatus();
        return true;
    }
    else if (DSPSignalNotification::match(cmd))
    {
        auto& notif = (const DSPSignalNotification&) cmd;
        m_centerFrequency = notif.getCenterFrequency();
        m_basebandSampleRate = notif.getSampleRate();

        // Forward to the source
        if (m_running)
        {
            auto* rep = new DSPSignalNotification(notif); // make a copy
            qDebug() << "VARAMod::handleMessage: DSPSignalNotification";
            m_basebandSource->getInputMessageQueue()->push(rep);
        }

        // Forward to GUI if any
        if (getMessageQueueToGUI()) {
            getMessageQueueToGUI()->push(new DSPSignalNotification(notif));
        }

        return true;
    }
    else
    {
        return false;
    }
}

void VARAMod::applySettings(const QStringList& settingsKeys, const VARAModSettings& settings, bool force)
{
    qDebug() << "VARAMod::applySettings:" << settings.getDebugString(settingsKeys, force);

    if (settingsKeys.contains("streamIndex") && (m_settings.m_streamIndex != settings.m_streamIndex))
    {
        if (m_deviceAPI->getSampleMIMO()) // change of stream is possible for MIMO devices only
        {
            m_deviceAPI->removeChannelSourceAPI(this);
            m_deviceAPI->removeChannelSource(this, false, m_settings.m_streamIndex);
            m_deviceAPI->addChannelSource(this, settings.m_streamIndex);
            m_deviceAPI->addChannelSourceAPI(this);
            m_settings.m_streamIndex = settings.m_streamIndex; // make sure ChannelAPI::getStreamIndex() is consistent
            emit streamIndexChanged(settings.m_streamIndex);
        }
    }

    if (m_running)
    {
        VARAModBaseband::MsgConfigureVARAModBaseband *msg = VARAModBaseband::MsgConfigureVARAModBaseband::create(settingsKeys, settings, force);
        m_basebandSource->getInputMessageQueue()->push(msg);
    }

    if (settingsKeys.contains("useReverseAPI"))
    {
        bool fullUpdate = (settingsKeys.contains("useReverseAPI") && settings.m_useReverseAPI) ||
                settingsKeys.contains("reverseAPIAddress") ||
                settingsKeys.contains("reverseAPIPort") ||
                settingsKeys.contains("reverseAPIDeviceIndex") ||
                settingsKeys.contains("reverseAPIChannelIndex");
        webapiReverseSendSettings(settingsKeys, settings, fullUpdate || force);
    }

    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(this, "settings", pipes);

    if (!pipes.empty()) {
        sendChannelSettings(pipes, settingsKeys, settings, force);
    }

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    if (settingsKeys.contains("pttAddress") || settingsKeys.contains("pttPort") || force) {
        m_ptt->setDestination(m_settings.m_pttAddress, m_settings.m_pttPort);
    }

    if (settingsKeys.contains("pttTailMs") || force) {
        m_ptt->setTailMs(m_settings.m_pttTailMs);
    }

    if (settingsKeys.contains("pttEnabled") || force)
    {
        *m_pttEnabled = m_settings.m_pttEnabled;

        if (!m_settings.m_pttEnabled) {
            m_ptt->setPtt(false);
        }
    }

    if (settingsKeys.contains("pttLeadMs") || settingsKeys.contains("txPathMs") || force) {
        applyTxTiming();
    }

    if (settingsKeys.contains("channelMute") || settingsKeys.contains("cmdPort") || force) {
        updateTxLink();
    }
}

VARAMod::Status VARAMod::getStatus() const
{
    Status status;
    status.m_pttReply = m_pttReply;

    if (m_txLink)
    {
        status.m_linked = true;
        status.m_engineAttached = m_txLink->getEngineAttached();
        status.m_engineMode = m_txLink->getEngineMode();
        status.m_keyed = m_txLink->getKeyed();
        status.m_fifoMs = m_txLink->getFill() * 1000 / VARAEngine::m_sampleRate;
        status.m_underflows = m_txLink->getUnderflows();
    }

    return status;
}

void VARAMod::pttReplyReceived(const QString& reply)
{
    m_pttReply = reply;
}

QByteArray VARAMod::serialize() const
{
    return m_settings.serialize();
}

bool VARAMod::deserialize(const QByteArray& data)
{
    bool success = true;

    if (!m_settings.deserialize(data))
    {
        m_settings.resetToDefaults();
        success = false;
    }

    MsgConfigureVARAMod *msg = MsgConfigureVARAMod::create(QStringList(), m_settings, true);
    m_inputMessageQueue.push(msg);

    return success;
}

int VARAMod::webapiSettingsGet(
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setVaraModSettings(new SWGSDRangel::SWGVARAModSettings());
    response.getVaraModSettings()->init();
    webapiFormatChannelSettings(response, m_settings);
    return 200;
}

int VARAMod::webapiWorkspaceGet(
        SWGSDRangel::SWGWorkspaceInfo& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setIndex(m_settings.m_workspaceIndex);
    return 200;
}

int VARAMod::webapiSettingsPutPatch(
        bool force,
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage)
{
    (void) errorMessage;
    VARAModSettings settings = m_settings;
    webapiUpdateChannelSettings(settings, channelSettingsKeys, response);

    MsgConfigureVARAMod *msg = MsgConfigureVARAMod::create(channelSettingsKeys, settings, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureVARAMod *msgToGUI = MsgConfigureVARAMod::create(channelSettingsKeys, settings, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatChannelSettings(response, settings);

    return 200;
}

void VARAMod::webapiUpdateChannelSettings(
        VARAModSettings& settings,
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings& response)
{
    SWGSDRangel::SWGVARAModSettings *swgSettings = response.getVaraModSettings();

    if (channelSettingsKeys.contains("inputFrequencyOffset")) {
        settings.m_inputFrequencyOffset = swgSettings->getInputFrequencyOffset();
    }
    if (channelSettingsKeys.contains("cmdPort")) {
        settings.m_cmdPort = swgSettings->getCmdPort();
    }
    if (channelSettingsKeys.contains("fmDeviation")) {
        settings.m_fmDeviation = swgSettings->getFmDeviation();
    }
    if (channelSettingsKeys.contains("gain")) {
        settings.m_gain = swgSettings->getGain();
    }
    if (channelSettingsKeys.contains("channelMute")) {
        settings.m_channelMute = swgSettings->getChannelMute() != 0;
    }
    if (channelSettingsKeys.contains("pttEnabled")) {
        settings.m_pttEnabled = swgSettings->getPttEnabled() != 0;
    }
    if (channelSettingsKeys.contains("pttAddress")) {
        settings.m_pttAddress = *swgSettings->getPttAddress();
    }
    if (channelSettingsKeys.contains("pttPort")) {
        settings.m_pttPort = swgSettings->getPttPort();
    }
    if (channelSettingsKeys.contains("pttLeadMs")) {
        settings.m_pttLeadMs = swgSettings->getPttLeadMs();
    }
    if (channelSettingsKeys.contains("pttTailMs")) {
        settings.m_pttTailMs = swgSettings->getPttTailMs();
    }
    if (channelSettingsKeys.contains("txPathMs")) {
        settings.m_txPathMs = swgSettings->getTxPathMs();
    }
    if (channelSettingsKeys.contains("linkFrequencyOffset")) {
        settings.m_linkFrequencyOffset = swgSettings->getLinkFrequencyOffset() != 0;
    }
    if (channelSettingsKeys.contains("rgbColor")) {
        settings.m_rgbColor = swgSettings->getRgbColor();
    }
    if (channelSettingsKeys.contains("title")) {
        settings.m_title = *swgSettings->getTitle();
    }
    if (settings.m_channelMarker && channelSettingsKeys.contains("channelMarker")) {
        settings.m_channelMarker->updateFrom(channelSettingsKeys, swgSettings->getChannelMarker());
    }
    if (channelSettingsKeys.contains("streamIndex")) {
        settings.m_streamIndex = swgSettings->getStreamIndex();
    }
    if (channelSettingsKeys.contains("useReverseAPI")) {
        settings.m_useReverseAPI = swgSettings->getUseReverseApi() != 0;
    }
    if (channelSettingsKeys.contains("reverseAPIAddress")) {
        settings.m_reverseAPIAddress = *swgSettings->getReverseApiAddress();
    }
    if (channelSettingsKeys.contains("reverseAPIPort")) {
        settings.m_reverseAPIPort = swgSettings->getReverseApiPort();
    }
    if (channelSettingsKeys.contains("reverseAPIDeviceIndex")) {
        settings.m_reverseAPIDeviceIndex = swgSettings->getReverseApiDeviceIndex();
    }
    if (channelSettingsKeys.contains("reverseAPIChannelIndex")) {
        settings.m_reverseAPIChannelIndex = swgSettings->getReverseApiChannelIndex();
    }
    if (settings.m_rollupState && channelSettingsKeys.contains("rollupState")) {
        settings.m_rollupState->updateFrom(channelSettingsKeys, swgSettings->getRollupState());
    }
}

int VARAMod::webapiReportGet(
        SWGSDRangel::SWGChannelReport& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setVaraModReport(new SWGSDRangel::SWGVARAModReport());
    response.getVaraModReport()->init();
    webapiFormatChannelReport(response);
    return 200;
}

void VARAMod::webapiFormatChannelSettings(SWGSDRangel::SWGChannelSettings& response, const VARAModSettings& settings)
{
    SWGSDRangel::SWGVARAModSettings *swgSettings = response.getVaraModSettings();

    swgSettings->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    swgSettings->setCmdPort(settings.m_cmdPort);
    swgSettings->setFmDeviation(settings.m_fmDeviation);
    swgSettings->setGain(settings.m_gain);
    swgSettings->setChannelMute(settings.m_channelMute ? 1 : 0);
    swgSettings->setPttEnabled(settings.m_pttEnabled ? 1 : 0);
    if (swgSettings->getPttAddress()) {
        *swgSettings->getPttAddress() = settings.m_pttAddress;
    } else {
        swgSettings->setPttAddress(new QString(settings.m_pttAddress));
    }
    swgSettings->setPttPort(settings.m_pttPort);
    swgSettings->setPttLeadMs(settings.m_pttLeadMs);
    swgSettings->setPttTailMs(settings.m_pttTailMs);
    swgSettings->setTxPathMs(settings.m_txPathMs);
    swgSettings->setLinkFrequencyOffset(settings.m_linkFrequencyOffset ? 1 : 0);
    swgSettings->setRgbColor(settings.m_rgbColor);
    if (swgSettings->getTitle()) {
        *swgSettings->getTitle() = settings.m_title;
    } else {
        swgSettings->setTitle(new QString(settings.m_title));
    }
    if (settings.m_channelMarker)
    {
        if (swgSettings->getChannelMarker())
        {
            settings.m_channelMarker->formatTo(swgSettings->getChannelMarker());
        }
        else
        {
            SWGSDRangel::SWGChannelMarker *swg = new SWGSDRangel::SWGChannelMarker();
            settings.m_channelMarker->formatTo(swg);
            swgSettings->setChannelMarker(swg);
        }
    }
    swgSettings->setStreamIndex(settings.m_streamIndex);
    swgSettings->setUseReverseApi(settings.m_useReverseAPI ? 1 : 0);
    if (swgSettings->getReverseApiAddress()) {
        *swgSettings->getReverseApiAddress() = settings.m_reverseAPIAddress;
    } else {
        swgSettings->setReverseApiAddress(new QString(settings.m_reverseAPIAddress));
    }
    swgSettings->setReverseApiPort(settings.m_reverseAPIPort);
    swgSettings->setReverseApiDeviceIndex(settings.m_reverseAPIDeviceIndex);
    swgSettings->setReverseApiChannelIndex(settings.m_reverseAPIChannelIndex);
    if (settings.m_rollupState)
    {
        if (swgSettings->getRollupState())
        {
            settings.m_rollupState->formatTo(swgSettings->getRollupState());
        }
        else
        {
            SWGSDRangel::SWGRollupState *swg = new SWGSDRangel::SWGRollupState();
            settings.m_rollupState->formatTo(swg);
            swgSettings->setRollupState(swg);
        }
    }
}

void VARAMod::webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response) const
{
    SWGSDRangel::SWGVARAModReport *swgReport = response.getVaraModReport();
    Status status = getStatus();

    swgReport->setChannelPowerDb(CalcDb::dbPower(getMagSq()));
    swgReport->setChannelSampleRate(getChannelSampleRate());
    swgReport->setEngineAttached(status.m_engineAttached ? 1 : 0);
    swgReport->setKeyed(status.m_keyed ? 1 : 0);
    swgReport->setFifoMs(status.m_fifoMs);
    swgReport->setUnderflows(status.m_underflows);
    *swgReport->getPttReply() = status.m_pttReply;
}

void VARAMod::webapiReverseSendSettings(const QList<QString>& channelSettingsKeys, const VARAModSettings& settings, bool force)
{
    SWGSDRangel::SWGChannelSettings *swgChannelSettings = new SWGSDRangel::SWGChannelSettings();
    webapiFormatChannelSettings(channelSettingsKeys, swgChannelSettings, settings, force);

    QString channelSettingsURL = QString("http://%1:%2/sdrangel/deviceset/%3/channel/%4/settings")
            .arg(settings.m_reverseAPIAddress)
            .arg(settings.m_reverseAPIPort)
            .arg(settings.m_reverseAPIDeviceIndex)
            .arg(settings.m_reverseAPIChannelIndex);
    m_networkRequest.setUrl(QUrl(channelSettingsURL));
    m_networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QBuffer *buffer = new QBuffer();
    buffer->open((QBuffer::ReadWrite));
    buffer->write(swgChannelSettings->asJson().toUtf8());
    buffer->seek(0);

    // Always use PATCH to avoid passing reverse API settings
    QNetworkReply *reply = m_networkManager->sendCustomRequest(m_networkRequest, "PATCH", buffer);
    buffer->setParent(reply);

    delete swgChannelSettings;
}

void VARAMod::sendChannelSettings(
    const QList<ObjectPipe*>& pipes,
    const QList<QString>& channelSettingsKeys,
    const VARAModSettings& settings,
    bool force)
{
    for (const auto& pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);

        if (messageQueue)
        {
            SWGSDRangel::SWGChannelSettings *swgChannelSettings = new SWGSDRangel::SWGChannelSettings();
            webapiFormatChannelSettings(channelSettingsKeys, swgChannelSettings, settings, force);
            MainCore::MsgChannelSettings *msg = MainCore::MsgChannelSettings::create(
                this,
                channelSettingsKeys,
                swgChannelSettings,
                force
            );
            messageQueue->push(msg);
        }
    }
}

void VARAMod::webapiFormatChannelSettings(
        const QList<QString>& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings *swgChannelSettings,
        const VARAModSettings& settings,
        bool force
)
{
    swgChannelSettings->setDirection(1); // single source (Tx)
    swgChannelSettings->setOriginatorChannelIndex(getIndexInDeviceSet());
    swgChannelSettings->setOriginatorDeviceSetIndex(getDeviceSetIndex());
    swgChannelSettings->setChannelType(new QString(m_channelId));
    swgChannelSettings->setVaraModSettings(new SWGSDRangel::SWGVARAModSettings());
    SWGSDRangel::SWGVARAModSettings *swgSettings = swgChannelSettings->getVaraModSettings();

    // transfer data that has been modified. When force is on transfer all data except reverse API data

    if (channelSettingsKeys.contains("inputFrequencyOffset") || force) {
        swgSettings->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    }
    if (channelSettingsKeys.contains("cmdPort") || force) {
        swgSettings->setCmdPort(settings.m_cmdPort);
    }
    if (channelSettingsKeys.contains("fmDeviation") || force) {
        swgSettings->setFmDeviation(settings.m_fmDeviation);
    }
    if (channelSettingsKeys.contains("gain") || force) {
        swgSettings->setGain(settings.m_gain);
    }
    if (channelSettingsKeys.contains("channelMute") || force) {
        swgSettings->setChannelMute(settings.m_channelMute ? 1 : 0);
    }
    if (channelSettingsKeys.contains("pttEnabled") || force) {
        swgSettings->setPttEnabled(settings.m_pttEnabled ? 1 : 0);
    }
    if (channelSettingsKeys.contains("pttAddress") || force) {
        swgSettings->setPttAddress(new QString(settings.m_pttAddress));
    }
    if (channelSettingsKeys.contains("pttPort") || force) {
        swgSettings->setPttPort(settings.m_pttPort);
    }
    if (channelSettingsKeys.contains("pttLeadMs") || force) {
        swgSettings->setPttLeadMs(settings.m_pttLeadMs);
    }
    if (channelSettingsKeys.contains("pttTailMs") || force) {
        swgSettings->setPttTailMs(settings.m_pttTailMs);
    }
    if (channelSettingsKeys.contains("txPathMs") || force) {
        swgSettings->setTxPathMs(settings.m_txPathMs);
    }
    if (channelSettingsKeys.contains("linkFrequencyOffset") || force) {
        swgSettings->setLinkFrequencyOffset(settings.m_linkFrequencyOffset ? 1 : 0);
    }
    if (channelSettingsKeys.contains("rgbColor") || force) {
        swgSettings->setRgbColor(settings.m_rgbColor);
    }
    if (channelSettingsKeys.contains("title") || force) {
        swgSettings->setTitle(new QString(settings.m_title));
    }
    if (settings.m_channelMarker && (channelSettingsKeys.contains("channelMarker") || force))
    {
        SWGSDRangel::SWGChannelMarker *swg = new SWGSDRangel::SWGChannelMarker();
        settings.m_channelMarker->formatTo(swg);
        swgSettings->setChannelMarker(swg);
    }
    if (channelSettingsKeys.contains("streamIndex") || force) {
        swgSettings->setStreamIndex(settings.m_streamIndex);
    }
    if (settings.m_rollupState && (channelSettingsKeys.contains("rollupState") || force))
    {
        SWGSDRangel::SWGRollupState *swg = new SWGSDRangel::SWGRollupState();
        settings.m_rollupState->formatTo(swg);
        swgSettings->setRollupState(swg);
    }
}

void VARAMod::networkManagerFinished(QNetworkReply *reply) const
{
    QNetworkReply::NetworkError replyError = reply->error();

    if (replyError)
    {
        qWarning() << "VARAMod::networkManagerFinished:"
                << " error(" << (int) replyError
                << "): " << replyError
                << ": " << reply->errorString();
    }
    else
    {
        QString answer = reply->readAll();
        answer.chop(1); // remove last \n
        qDebug("VARAMod::networkManagerFinished: reply:\n%s", answer.toStdString().c_str());
    }

    reply->deleteLater();
}

double VARAMod::getMagSq() const
{
    if (m_running) {
        return m_basebandSource->getMagSq();
    }

    return 0;
}

uint32_t VARAMod::getNumberOfDeviceStreams() const
{
    return m_deviceAPI->getNbSinkStreams();
}

int VARAMod::getChannelSampleRate() const
{
    return m_basebandSource ? m_basebandSource->getChannelSampleRate() : 0;
}
