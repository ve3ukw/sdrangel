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
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QThread>

#include "SWGChannelSettings.h"
#include "SWGWorkspaceInfo.h"
#include "SWGSSDVModSettings.h"
#include "SWGChannelReport.h"
#include "SWGSSDVModReport.h"
#include "SWGChannelActions.h"
#include "SWGSSDVModActions.h"

#include "dsp/dspcommands.h"
#include "device/deviceapi.h"
#include "settings/serializable.h"
#include "util/db.h"
#include "util/ssdvcodec.h"
#include "maincore.h"

#include "ssdvmodreport.h"
#include "ssdvmodbaseband.h"
#include "ssdvmod.h"

MESSAGE_CLASS_DEFINITION(SSDVMod::MsgConfigureSSDVMod, Message)
MESSAGE_CLASS_DEFINITION(SSDVMod::MsgTx, Message)
MESSAGE_CLASS_DEFINITION(SSDVMod::MsgStop, Message)

const char* const SSDVMod::m_channelIdURI = "sdrangel.channeltx.modssdv";
const char* const SSDVMod::m_channelId = "SSDVMod";

SSDVMod::SSDVMod(DeviceAPI *deviceAPI) :
    ChannelAPI(m_channelIdURI, ChannelAPI::StreamSingleSource),
    m_deviceAPI(deviceAPI),
    m_transmitting(false),
    m_stopped(false),
    m_txImageId(-1),
    m_packetsSent(0),
    m_packetCount(0),
    m_imagesSent(0),
    m_fileIndex(0)
{
    setObjectName(m_channelId);

    m_thread = new QThread(this);
    m_thread->setObjectName("SSDVMod");
    m_basebandSource = new SSDVModBaseband();
    m_basebandSource->setChannel(this);
    m_basebandSource->setMessageQueueToChannel(getInputMessageQueue());
    m_basebandSource->moveToThread(m_thread);

    applySettings(QStringList(), m_settings, true);

    m_deviceAPI->addChannelSource(this);
    m_deviceAPI->addChannelSourceAPI(this);

    m_repeatTimer.setSingleShot(true);
    QObject::connect(&m_repeatTimer, &QTimer::timeout, this, &SSDVMod::repeat);

    m_networkManager = new QNetworkAccessManager();
    QObject::connect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &SSDVMod::networkManagerFinished
    );
}

SSDVMod::~SSDVMod()
{
    QObject::disconnect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &SSDVMod::networkManagerFinished
    );
    delete m_networkManager;
    m_deviceAPI->removeChannelSourceAPI(this);
    m_deviceAPI->removeChannelSource(this, true);
    stop();
    delete m_basebandSource;
    delete m_thread;
}

void SSDVMod::setDeviceAPI(DeviceAPI *deviceAPI)
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

void SSDVMod::start()
{
    qDebug("SSDVMod::start");
    m_basebandSource->reset();
    m_thread->start();
}

void SSDVMod::stop()
{
    qDebug("SSDVMod::stop");
    m_thread->exit();
    m_thread->wait();
}

void SSDVMod::pull(SampleVector::iterator& begin, unsigned int nbSamples)
{
    m_basebandSource->pull(begin, nbSamples);
}

QStringList SSDVMod::getImageFiles(const QString& filenameOrDirectory)
{
    QFileInfo info(filenameOrDirectory);

    if (!info.isDir()) {
        return info.isFile() ? QStringList(filenameOrDirectory) : QStringList();
    }

    QStringList filters;

    for (const auto& format : QImageReader::supportedImageFormats()) {
        filters.append("*." + QString(format));
    }

    QDir dir(filenameOrDirectory);
    QStringList files;

    for (const auto& name : dir.entryList(filters, QDir::Files | QDir::Readable, QDir::Name)) {
        files.append(dir.filePath(name));
    }

    return files;
}

void SSDVMod::setError(const QString& error)
{
    qWarning() << "SSDVMod:" << error;
    m_error = error;

    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(SSDVModReport::MsgImage::create(QImage(), QString(), m_settings.m_imageId, 0, error));
    }
}

bool SSDVMod::transmitNextImage()
{
    QStringList files = getImageFiles(m_settings.m_imageFilename);

    if (files.isEmpty())
    {
        setError(m_settings.m_imageFilename.isEmpty() ? tr("No image selected") : tr("No images in %1").arg(m_settings.m_imageFilename));
        return false;
    }

    QString filename = files[m_fileIndex % files.size()];
    m_fileIndex = (m_fileIndex + 1) % files.size();
    QImage image(filename);

    if (image.isNull())
    {
        setError(tr("Could not read %1").arg(filename));
        return false;
    }

    int width = std::max(16, m_settings.m_width & ~15);
    int height = std::max(16, m_settings.m_height & ~15);
    image = SSDV::Encoder::fit(image, width, height, m_settings.m_fill);

    SSDV::Encoder encoder(
        m_settings.m_packetFormat == SSDVModSettings::AO123 ? SSDV::DSLWP : SSDV::Standard,
        m_settings.m_callsign,
        m_settings.m_imageId & 0xFF,
        std::max(0, std::min(7, m_settings.m_quality)),
        true
    );
    QString error;
    QList<QByteArray> packets = encoder.encode(image, error);

    if (packets.isEmpty())
    {
        setError(tr("Could not encode %1: %2").arg(filename).arg(error));
        return false;
    }

    std::vector<uint8_t> symbols;
    std::vector<std::pair<int, int>> progress;
    m_framer.build(m_settings, packets, symbols, progress);
    m_basebandSource->getInputMessageQueue()->push(SSDVModReport::MsgTransmit::create(symbols, progress));

    m_transmitting = true;
    m_txImageId = m_settings.m_imageId & 0xFF;
    m_packetsSent = 0;
    m_packetCount = packets.size();
    m_error.clear();
    qDebug() << "SSDVMod::transmitNextImage:" << filename << "image ID" << m_txImageId << packets.size() << "packets"
        << symbols.size() / (double) m_settings.m_baudRate << "s";

    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(SSDVModReport::MsgImage::create(image, filename, m_txImageId, m_packetCount, QString()));
    }

    // Next image gets the next ID
    SSDVModSettings settings = m_settings;
    settings.m_imageId = (m_settings.m_imageId + 1) & 0xFF;
    applySettings(QStringList("imageId"), settings);

    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(MsgConfigureSSDVMod::create(QStringList("imageId"), settings, false));
    }

    return true;
}

void SSDVMod::repeat()
{
    if (!m_stopped && !m_transmitting && !transmitNextImage()) {
        m_stopped = true;
    }
}

bool SSDVMod::handleMessage(const Message& cmd)
{
    if (MsgConfigureSSDVMod::match(cmd))
    {
        MsgConfigureSSDVMod& cfg = (MsgConfigureSSDVMod&) cmd;
        qDebug() << "SSDVMod::handleMessage: MsgConfigureSSDVMod";

        applySettings(cfg.getSettingsKeys(), cfg.getSettings(), cfg.getForce());

        return true;
    }
    else if (MsgTx::match(cmd))
    {
        m_stopped = false;
        m_repeatTimer.stop();

        if (!m_transmitting) {
            transmitNextImage();
        }

        return true;
    }
    else if (MsgStop::match(cmd))
    {
        m_stopped = true;
        m_repeatTimer.stop();
        m_basebandSource->getInputMessageQueue()->push(SSDVModReport::MsgAbort::create());

        return true;
    }
    else if (SSDVModReport::MsgProgress::match(cmd))
    {
        const SSDVModReport::MsgProgress& report = (const SSDVModReport::MsgProgress&) cmd;
        m_packetsSent = report.getPacketsSent();

        if (report.getDone())
        {
            m_transmitting = false;

            if (m_packetsSent >= m_packetCount) {
                m_imagesSent++;
            }

            if (m_settings.m_repeat && !m_stopped) {
                m_repeatTimer.start(std::max(0, (int) (m_settings.m_repeatDelay * 1000.0f)));
            }
        }

        if (getMessageQueueToGUI()) {
            getMessageQueueToGUI()->push(SSDVModReport::MsgProgress::create(report.getPacketsSent(), report.getPacketCount(), report.getDone()));
        }

        return true;
    }
    else if (DSPSignalNotification::match(cmd))
    {
        // Forward to the source
        DSPSignalNotification& notif = (DSPSignalNotification&) cmd;
        DSPSignalNotification* rep = new DSPSignalNotification(notif); // make a copy
        qDebug() << "SSDVMod::handleMessage: DSPSignalNotification";
        m_basebandSource->getInputMessageQueue()->push(rep);

        // Forward to GUI if any
        if (getMessageQueueToGUI()) {
            getMessageQueueToGUI()->push(new DSPSignalNotification(notif));
        }

        return true;
    }
    else if (MainCore::MsgChannelDemodQuery::match(cmd))
    {
        qDebug() << "SSDVMod::handleMessage: MsgChannelDemodQuery";
        sendSampleRateToDemodAnalyzer();

        return true;
    }
    else
    {
        return false;
    }
}

void SSDVMod::setCenterFrequency(qint64 frequency)
{
    SSDVModSettings settings = m_settings;
    settings.m_inputFrequencyOffset = frequency;
    applySettings(QStringList("inputFrequencyOffset"), settings, false);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureSSDVMod *msgToGUI = MsgConfigureSSDVMod::create(QStringList("inputFrequencyOffset"), settings, false);
        m_guiMessageQueue->push(msgToGUI);
    }
}

void SSDVMod::applySettings(const QStringList& settingsKeys, const SSDVModSettings& settings, bool force)
{
    qDebug() << "SSDVMod::applySettings:" << settings.getDebugString(settingsKeys, force);

    if (settingsKeys.contains("streamIndex") && m_settings.m_streamIndex != settings.m_streamIndex)
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

    if (settingsKeys.contains("imageFilename") && (settings.m_imageFilename != m_settings.m_imageFilename)) {
        m_fileIndex = 0;
    }

    if (settingsKeys.contains("repeat") && !settings.m_repeat) {
        m_repeatTimer.stop();
    }

    SSDVModBaseband::MsgConfigureSSDVModBaseband *msg = SSDVModBaseband::MsgConfigureSSDVModBaseband::create(settingsKeys, settings, force);
    m_basebandSource->getInputMessageQueue()->push(msg);

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

    if (pipes.size() > 0) {
        sendChannelSettings(pipes, settingsKeys, settings, force);
    }

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }
}

QByteArray SSDVMod::serialize() const
{
    return m_settings.serialize();
}

bool SSDVMod::deserialize(const QByteArray& data)
{
    bool success = true;

    if (!m_settings.deserialize(data))
    {
        m_settings.resetToDefaults();
        success = false;
    }

    MsgConfigureSSDVMod *msg = MsgConfigureSSDVMod::create(QStringList(), m_settings, true);
    m_inputMessageQueue.push(msg);

    return success;
}

void SSDVMod::sendSampleRateToDemodAnalyzer()
{
    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(this, "reportdemod", pipes);

    for (const auto& pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);
        MainCore::MsgChannelDemodReport *msg = MainCore::MsgChannelDemodReport::create(
            this,
            getChannelSampleRate()
        );
        messageQueue->push(msg);
    }
}

int SSDVMod::webapiSettingsGet(
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setSsdvModSettings(new SWGSDRangel::SWGSSDVModSettings());
    response.getSsdvModSettings()->init();
    webapiFormatChannelSettings(response, m_settings);
    return 200;
}

int SSDVMod::webapiWorkspaceGet(
        SWGSDRangel::SWGWorkspaceInfo& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setIndex(m_settings.m_workspaceIndex);
    return 200;
}

int SSDVMod::webapiSettingsPutPatch(
        bool force,
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage)
{
    (void) errorMessage;
    SSDVModSettings settings = m_settings;
    webapiUpdateChannelSettings(settings, channelSettingsKeys, response);

    MsgConfigureSSDVMod *msg = MsgConfigureSSDVMod::create(channelSettingsKeys, settings, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureSSDVMod *msgToGUI = MsgConfigureSSDVMod::create(channelSettingsKeys, settings, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatChannelSettings(response, settings);

    return 200;
}

void SSDVMod::webapiUpdateChannelSettings(
        SSDVModSettings& settings,
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings& response)
{
    SWGSDRangel::SWGSSDVModSettings *swgSettings = response.getSsdvModSettings();

    if (channelSettingsKeys.contains("inputFrequencyOffset")) {
        settings.m_inputFrequencyOffset = swgSettings->getInputFrequencyOffset();
    }
    if (channelSettingsKeys.contains("baudRate")) {
        settings.m_baudRate = swgSettings->getBaudRate();
    }
    if (channelSettingsKeys.contains("rrcRolloff")) {
        settings.m_rrcRolloff = swgSettings->getRrcRolloff();
    }
    if (channelSettingsKeys.contains("gain")) {
        settings.m_gain = swgSettings->getGain();
    }
    if (channelSettingsKeys.contains("channelMute")) {
        settings.m_channelMute = swgSettings->getChannelMute() != 0;
    }
    if (channelSettingsKeys.contains("frameSize")) {
        settings.m_frameSize = swgSettings->getFrameSize();
    }
    if (channelSettingsKeys.contains("rs")) {
        settings.m_rs = swgSettings->getRs() != 0;
    }
    if (channelSettingsKeys.contains("dualBasis")) {
        settings.m_dualBasis = swgSettings->getDualBasis() != 0;
    }
    if (channelSettingsKeys.contains("randomizer")) {
        settings.m_randomizer = swgSettings->getRandomizer() != 0;
    }
    if (channelSettingsKeys.contains("differential")) {
        settings.m_differential = swgSettings->getDifferential() != 0;
    }
    if (channelSettingsKeys.contains("convolutional")) {
        settings.m_convolutional = swgSettings->getConvolutional() != 0;
    }
    if (channelSettingsKeys.contains("packetFormat")) {
        settings.m_packetFormat = swgSettings->getPacketFormat();
    }
    if (channelSettingsKeys.contains("callsign")) {
        settings.m_callsign = *swgSettings->getCallsign();
    }
    if (channelSettingsKeys.contains("spacecraftId")) {
        settings.m_spacecraftId = swgSettings->getSpacecraftId();
    }
    if (channelSettingsKeys.contains("virtualChannel")) {
        settings.m_virtualChannel = swgSettings->getVirtualChannel();
    }
    if (channelSettingsKeys.contains("imageFilename")) {
        settings.m_imageFilename = *swgSettings->getImageFilename();
    }
    if (channelSettingsKeys.contains("imageId")) {
        settings.m_imageId = swgSettings->getImageId();
    }
    if (channelSettingsKeys.contains("quality")) {
        settings.m_quality = swgSettings->getQuality();
    }
    if (channelSettingsKeys.contains("width")) {
        settings.m_width = swgSettings->getWidth();
    }
    if (channelSettingsKeys.contains("height")) {
        settings.m_height = swgSettings->getHeight();
    }
    if (channelSettingsKeys.contains("fill")) {
        settings.m_fill = swgSettings->getFill() != 0;
    }
    if (channelSettingsKeys.contains("preambleMs")) {
        settings.m_preambleMs = swgSettings->getPreambleMs();
    }
    if (channelSettingsKeys.contains("repeat")) {
        settings.m_repeat = swgSettings->getRepeat() != 0;
    }
    if (channelSettingsKeys.contains("repeatDelay")) {
        settings.m_repeatDelay = swgSettings->getRepeatDelay();
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

int SSDVMod::webapiReportGet(
        SWGSDRangel::SWGChannelReport& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setSsdvModReport(new SWGSDRangel::SWGSSDVModReport());
    response.getSsdvModReport()->init();
    webapiFormatChannelReport(response);
    return 200;
}

int SSDVMod::webapiActionsPost(
        const QStringList& channelActionsKeys,
        SWGSDRangel::SWGChannelActions& query,
        QString& errorMessage)
{
    SWGSDRangel::SWGSSDVModActions *swgActions = query.getSsdvModActions();

    if (!swgActions)
    {
        errorMessage = "Missing SSDVModActions in query";
        return 400;
    }

    if (channelActionsKeys.contains("stop") && (swgActions->getStop() != 0))
    {
        m_inputMessageQueue.push(MsgStop::create());
        return 202;
    }

    if (channelActionsKeys.contains("tx") && (swgActions->getTx() != 0))
    {
        m_inputMessageQueue.push(MsgTx::create());
        return 202;
    }

    errorMessage = "Must contain tx or stop action";
    return 400;
}

void SSDVMod::webapiFormatChannelSettings(SWGSDRangel::SWGChannelSettings& response, const SSDVModSettings& settings)
{
    SWGSDRangel::SWGSSDVModSettings *swgSettings = response.getSsdvModSettings();

    swgSettings->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    swgSettings->setBaudRate(settings.m_baudRate);
    swgSettings->setRrcRolloff(settings.m_rrcRolloff);
    swgSettings->setGain(settings.m_gain);
    swgSettings->setChannelMute(settings.m_channelMute ? 1 : 0);
    swgSettings->setFrameSize(settings.m_frameSize);
    swgSettings->setRs(settings.m_rs ? 1 : 0);
    swgSettings->setDualBasis(settings.m_dualBasis ? 1 : 0);
    swgSettings->setRandomizer(settings.m_randomizer ? 1 : 0);
    swgSettings->setDifferential(settings.m_differential ? 1 : 0);
    swgSettings->setConvolutional(settings.m_convolutional ? 1 : 0);
    swgSettings->setPacketFormat(settings.m_packetFormat);
    if (swgSettings->getCallsign()) {
        *swgSettings->getCallsign() = settings.m_callsign;
    } else {
        swgSettings->setCallsign(new QString(settings.m_callsign));
    }
    swgSettings->setSpacecraftId(settings.m_spacecraftId);
    swgSettings->setVirtualChannel(settings.m_virtualChannel);
    if (swgSettings->getImageFilename()) {
        *swgSettings->getImageFilename() = settings.m_imageFilename;
    } else {
        swgSettings->setImageFilename(new QString(settings.m_imageFilename));
    }
    swgSettings->setImageId(settings.m_imageId);
    swgSettings->setQuality(settings.m_quality);
    swgSettings->setWidth(settings.m_width);
    swgSettings->setHeight(settings.m_height);
    swgSettings->setFill(settings.m_fill ? 1 : 0);
    swgSettings->setPreambleMs(settings.m_preambleMs);
    swgSettings->setRepeat(settings.m_repeat ? 1 : 0);
    swgSettings->setRepeatDelay(settings.m_repeatDelay);
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

void SSDVMod::webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response)
{
    SWGSDRangel::SWGSSDVModReport *swgReport = response.getSsdvModReport();
    swgReport->setChannelPowerDb(CalcDb::dbPower(getMagSq()));
    swgReport->setChannelSampleRate(m_basebandSource->getChannelSampleRate());
    swgReport->setTransmitting(m_transmitting ? 1 : 0);
    swgReport->setImageId(m_txImageId);
    swgReport->setPacketsSent(m_packetsSent);
    swgReport->setPacketCount(m_packetCount);
    swgReport->setImagesSent(m_imagesSent);
    *swgReport->getError() = m_error;
}

void SSDVMod::webapiReverseSendSettings(const QList<QString>& channelSettingsKeys, const SSDVModSettings& settings, bool force)
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

void SSDVMod::sendChannelSettings(
    const QList<ObjectPipe*>& pipes,
    const QList<QString>& channelSettingsKeys,
    const SSDVModSettings& settings,
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

void SSDVMod::webapiFormatChannelSettings(
        const QList<QString>& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings *swgChannelSettings,
        const SSDVModSettings& settings,
        bool force
)
{
    swgChannelSettings->setDirection(1); // Single source (Tx)
    swgChannelSettings->setOriginatorChannelIndex(getIndexInDeviceSet());
    swgChannelSettings->setOriginatorDeviceSetIndex(getDeviceSetIndex());
    swgChannelSettings->setChannelType(new QString(m_channelId));
    swgChannelSettings->setSsdvModSettings(new SWGSDRangel::SWGSSDVModSettings());
    SWGSDRangel::SWGSSDVModSettings *swgSettings = swgChannelSettings->getSsdvModSettings();

    // transfer data that has been modified. When force is on transfer all data except reverse API data

    if (channelSettingsKeys.contains("inputFrequencyOffset") || force) {
        swgSettings->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    }
    if (channelSettingsKeys.contains("baudRate") || force) {
        swgSettings->setBaudRate(settings.m_baudRate);
    }
    if (channelSettingsKeys.contains("rrcRolloff") || force) {
        swgSettings->setRrcRolloff(settings.m_rrcRolloff);
    }
    if (channelSettingsKeys.contains("gain") || force) {
        swgSettings->setGain(settings.m_gain);
    }
    if (channelSettingsKeys.contains("channelMute") || force) {
        swgSettings->setChannelMute(settings.m_channelMute ? 1 : 0);
    }
    if (channelSettingsKeys.contains("frameSize") || force) {
        swgSettings->setFrameSize(settings.m_frameSize);
    }
    if (channelSettingsKeys.contains("rs") || force) {
        swgSettings->setRs(settings.m_rs ? 1 : 0);
    }
    if (channelSettingsKeys.contains("dualBasis") || force) {
        swgSettings->setDualBasis(settings.m_dualBasis ? 1 : 0);
    }
    if (channelSettingsKeys.contains("randomizer") || force) {
        swgSettings->setRandomizer(settings.m_randomizer ? 1 : 0);
    }
    if (channelSettingsKeys.contains("differential") || force) {
        swgSettings->setDifferential(settings.m_differential ? 1 : 0);
    }
    if (channelSettingsKeys.contains("convolutional") || force) {
        swgSettings->setConvolutional(settings.m_convolutional ? 1 : 0);
    }
    if (channelSettingsKeys.contains("packetFormat") || force) {
        swgSettings->setPacketFormat(settings.m_packetFormat);
    }
    if (channelSettingsKeys.contains("callsign") || force) {
        swgSettings->setCallsign(new QString(settings.m_callsign));
    }
    if (channelSettingsKeys.contains("spacecraftId") || force) {
        swgSettings->setSpacecraftId(settings.m_spacecraftId);
    }
    if (channelSettingsKeys.contains("virtualChannel") || force) {
        swgSettings->setVirtualChannel(settings.m_virtualChannel);
    }
    if (channelSettingsKeys.contains("imageFilename") || force) {
        swgSettings->setImageFilename(new QString(settings.m_imageFilename));
    }
    if (channelSettingsKeys.contains("imageId") || force) {
        swgSettings->setImageId(settings.m_imageId);
    }
    if (channelSettingsKeys.contains("quality") || force) {
        swgSettings->setQuality(settings.m_quality);
    }
    if (channelSettingsKeys.contains("width") || force) {
        swgSettings->setWidth(settings.m_width);
    }
    if (channelSettingsKeys.contains("height") || force) {
        swgSettings->setHeight(settings.m_height);
    }
    if (channelSettingsKeys.contains("fill") || force) {
        swgSettings->setFill(settings.m_fill ? 1 : 0);
    }
    if (channelSettingsKeys.contains("preambleMs") || force) {
        swgSettings->setPreambleMs(settings.m_preambleMs);
    }
    if (channelSettingsKeys.contains("repeat") || force) {
        swgSettings->setRepeat(settings.m_repeat ? 1 : 0);
    }
    if (channelSettingsKeys.contains("repeatDelay") || force) {
        swgSettings->setRepeatDelay(settings.m_repeatDelay);
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

void SSDVMod::networkManagerFinished(QNetworkReply *reply)
{
    QNetworkReply::NetworkError replyError = reply->error();

    if (replyError)
    {
        qWarning() << "SSDVMod::networkManagerFinished:"
                << " error(" << (int) replyError
                << "): " << replyError
                << ": " << reply->errorString();
    }
    else
    {
        QString answer = reply->readAll();
        answer.chop(1); // remove last \n
        qDebug("SSDVMod::networkManagerFinished: reply:\n%s", answer.toStdString().c_str());
    }

    reply->deleteLater();
}

double SSDVMod::getMagSq() const
{
    return m_basebandSource->getMagSq();
}

void SSDVMod::setLevelMeter(QObject *levelMeter)
{
    connect(m_basebandSource, SIGNAL(levelChanged(qreal, qreal, int)), levelMeter, SLOT(levelChanged(qreal, qreal, int)));
}

uint32_t SSDVMod::getNumberOfDeviceStreams() const
{
    return m_deviceAPI->getNbSinkStreams();
}

int SSDVMod::getChannelSampleRate() const
{
    return m_basebandSource ? m_basebandSource->getChannelSampleRate() : 0;
}
