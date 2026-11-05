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
#include <QDateTime>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QBuffer>
#include <QStandardPaths>
#include <QThread>

#include "SWGChannelSettings.h"
#include "SWGWorkspaceInfo.h"
#include "SWGSSDVDemodSettings.h"
#include "SWGChannelReport.h"
#include "SWGSSDVDemodReport.h"

#include "dsp/dspcommands.h"
#include "device/deviceapi.h"
#include "settings/serializable.h"
#include "util/ccsds.h"
#include "util/db.h"
#include "maincore.h"

#include "ssdvdemodreport.h"
#include "ssdvdemod.h"

MESSAGE_CLASS_DEFINITION(SSDVDemod::MsgConfigureSSDVDemod, Message)
MESSAGE_CLASS_DEFINITION(SSDVDemod::MsgClear, Message)

const char* const SSDVDemod::m_channelIdURI = "sdrangel.channel.ssdvdemod";
const char* const SSDVDemod::m_channelId = "SSDVDemod";

SSDVDemod::SSDVDemod(DeviceAPI *deviceAPI) :
    ChannelAPI(m_channelIdURI, ChannelAPI::StreamSingleSink),
    m_deviceAPI(deviceAPI),
    m_thread(nullptr),
    m_basebandSink(nullptr),
    m_running(false),
    m_basebandSampleRate(0),
    m_centerFrequency(0),
    m_framesReceived(0),
    m_framesBad(0),
    m_packetsReceived(0),
    m_imagesReceived(0)
{
    qDebug("SSDVDemod::SSDVDemod");
    setObjectName(m_channelId);
    applySettings(QStringList(), m_settings, true);

    m_deviceAPI->addChannelSink(this);
    m_deviceAPI->addChannelSinkAPI(this);

    m_networkManager = new QNetworkAccessManager();
    QObject::connect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &SSDVDemod::networkManagerFinished
    );
    QObject::connect(
        this,
        &ChannelAPI::indexInDeviceSetChanged,
        this,
        &SSDVDemod::handleIndexInDeviceSetChanged
    );
}

SSDVDemod::~SSDVDemod()
{
    QObject::disconnect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &SSDVDemod::networkManagerFinished
    );
    delete m_networkManager;
    m_deviceAPI->removeChannelSinkAPI(this);
    m_deviceAPI->removeChannelSink(this, true);

    stop();
    clearImages();
}

void SSDVDemod::setDeviceAPI(DeviceAPI *deviceAPI)
{
    if (deviceAPI != m_deviceAPI)
    {
        m_deviceAPI->removeChannelSinkAPI(this);
        m_deviceAPI->removeChannelSink(this, false);
        m_deviceAPI = deviceAPI;
        m_deviceAPI->addChannelSink(this);
        m_deviceAPI->addChannelSinkAPI(this);
    }
}

uint32_t SSDVDemod::getNumberOfDeviceStreams() const
{
    return m_deviceAPI->getNbSourceStreams();
}

void SSDVDemod::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, bool firstOfBurst)
{
    (void) firstOfBurst;
    m_basebandSink->feed(begin, end);
}

void SSDVDemod::start()
{
    if (m_running) {
        return;
    }

    qDebug() << "SSDVDemod::start";
    m_thread = new QThread();
    m_thread->setObjectName("SSDVDemod");
    m_basebandSink = new SSDVDemodBaseband();
    m_basebandSink->setFifoLabel(QString("%1 [%2:%3]")
        .arg(m_channelId)
        .arg(m_deviceAPI->getDeviceSetIndex())
        .arg(getIndexInDeviceSet())
    );
    m_basebandSink->setChannel(this);
    m_basebandSink->setScopeSink(&m_scopeVis);
    m_basebandSink->setMessageQueueToChannel(getInputMessageQueue());
    m_basebandSink->moveToThread(m_thread);

    QObject::connect(
        m_thread,
        &QThread::finished,
        m_basebandSink,
        &QObject::deleteLater
    );
    QObject::connect(
        m_thread,
        &QThread::finished,
        m_thread,
        &QThread::deleteLater
    );

    m_thread->start();

    DSPSignalNotification *dspMsg = new DSPSignalNotification(m_basebandSampleRate, m_centerFrequency);
    m_basebandSink->getInputMessageQueue()->push(dspMsg);

    SSDVDemodBaseband::MsgConfigureSSDVDemodBaseband *msg = SSDVDemodBaseband::MsgConfigureSSDVDemodBaseband::create(QStringList(), m_settings, true);
    m_basebandSink->getInputMessageQueue()->push(msg);

    m_running = true;
}

void SSDVDemod::stop()
{
    if (!m_running) {
        return;
    }

    qDebug() << "SSDVDemod::stop";
    m_running = false;
    m_framesBad += m_basebandSink->getFramesBad();
    m_thread->quit();
    m_thread->wait();
    m_basebandSink = nullptr; // Freed by deleteLater when the thread finished
    m_framesFile.close();
}

bool SSDVDemod::handleMessage(const Message& cmd)
{
    if (MsgConfigureSSDVDemod::match(cmd))
    {
        const MsgConfigureSSDVDemod& cfg = (const MsgConfigureSSDVDemod&) cmd;
        qDebug() << "SSDVDemod::handleMessage: MsgConfigureSSDVDemod";
        applySettings(cfg.getSettingsKeys(), cfg.getSettings(), cfg.getForce());

        return true;
    }
    else if (MsgClear::match(cmd))
    {
        clearImages();
        return true;
    }
    else if (SSDVDemodReport::MsgFrame::match(cmd))
    {
        const SSDVDemodReport::MsgFrame& report = (const SSDVDemodReport::MsgFrame&) cmd;
        m_framesReceived++;

        if (m_settings.m_saveFrames) {
            saveFrame(report.getData());
        }

        if (getMessageQueueToGUI())
        {
            getMessageQueueToGUI()->push(SSDVDemodReport::MsgFrame::create(
                report.getData(), report.getRSErrors(), report.getASMErrors(), report.getDateTime()));
        }

        handleFrame(report.getData(), report.getDateTime());

        return true;
    }
    else if (DSPSignalNotification::match(cmd))
    {
        qDebug() << "SSDVDemod::handleMessage: DSPSignalNotification";
        const DSPSignalNotification& notif = (const DSPSignalNotification&) cmd;
        m_basebandSampleRate = notif.getSampleRate();
        m_centerFrequency = notif.getCenterFrequency();

        // Forward to the sink if any
        if (m_running) {
            m_basebandSink->getInputMessageQueue()->push(new DSPSignalNotification(notif));
        }

        // Forward to GUI if any
        if (getMessageQueueToGUI()) {
            getMessageQueueToGUI()->push(new DSPSignalNotification(notif));
        }

        return true;
    }
    else if (MainCore::MsgChannelDemodQuery::match(cmd))
    {
        qDebug() << "SSDVDemod::handleMessage: MsgChannelDemodQuery";
        sendSampleRateToDemodAnalyzer();

        return true;
    }
    else
    {
        return false;
    }
}

void SSDVDemod::handleFrame(const QByteArray& data, const QDateTime& dateTime)
{
    for (const auto& packet : m_frameParser.parse(data)) {
        handlePacket(packet.m_data, packet.m_variant, dateTime);
    }
}

void SSDVDemod::handlePacket(const QByteArray& data, SSDV::Variant variant, const QDateTime& dateTime)
{
    SSDV::PacketInfo info;
    QByteArray standardPacket;

    if (!SSDV::parsePacket(data, variant, info, standardPacket)) {
        return;
    }

    m_packetsReceived++;

    // Find the image the packet belongs to, or start a new one
    int index = -1;

    for (int i = 0; i < m_images.size(); i++)
    {
        SSDV::ImageDecoder *decoder = m_images[i].m_decoder;

        if ((decoder->getCallsign() == info.m_callsign) && (decoder->getImageId() == info.m_imageId)
            && (decoder->getWidth() == info.m_width) && (decoder->getHeight() == info.m_height))
        {
            index = i;
            break;
        }
    }

    if (index < 0)
    {
        if (m_images.size() >= m_maxImages)
        {
            Image& oldest = m_images.last();

            if (!oldest.m_saved) {
                autoSaveImage(oldest, false);
            }

            delete oldest.m_decoder;
            m_images.removeLast();
        }

        Image image;
        image.m_decoder = new SSDV::ImageDecoder(info.m_callsign, info.m_imageId, standardPacket.size());
        image.m_saved = false;
        m_images.prepend(image);
        index = 0;
    }
    else if (index > 0)
    {
        m_images.move(index, 0);
        index = 0;
    }

    Image& image = m_images[0];
    bool repeat = !image.m_decoder->addPacket(standardPacket, info);

    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(SSDVDemodReport::MsgPacket::create(info, dateTime, repeat));
    }

    if (repeat) {
        return;
    }

    SSDV::ImageDecoder *decoder = image.m_decoder;
    QString savedFile;

    if (decoder->isComplete() && !image.m_saved)
    {
        m_imagesReceived++;
        savedFile = autoSaveImage(image, true);
        image.m_saved = true; // Even when not saved, so a repeat doesn't count as another image
    }

    if (getMessageQueueToGUI())
    {
        getMessageQueueToGUI()->push(SSDVDemodReport::MsgImage::create(
            decoder->image(),
            decoder->getCallsign(),
            decoder->getImageId(),
            decoder->getPacketsReceived(),
            decoder->getPacketCount(),
            decoder->getMissingPackets(),
            decoder->getProgress(),
            decoder->isComplete(),
            savedFile
        ));
    }
}

void SSDVDemod::saveFrame(const QByteArray& data)
{
    // KISS framed, like gr-satellites --kiss_out
    static const char fend = (char) 0xC0;
    static const char fesc = (char) 0xDB;
    static const char tfend = (char) 0xDC;
    static const char tfesc = (char) 0xDD;

    if (!m_framesFile.isOpen() || (m_framesFile.fileName() != m_settings.m_framesFilename))
    {
        m_framesFile.close();
        m_framesFile.setFileName(m_settings.m_framesFilename);

        if (!m_framesFile.open(QIODevice::WriteOnly | QIODevice::Append))
        {
            qWarning() << "SSDVDemod::saveFrame: could not open" << m_settings.m_framesFilename;
            return;
        }
    }

    QByteArray kiss;
    kiss.append(fend);
    kiss.append((char) 0x00);

    for (char c : data)
    {
        if (c == fend)
        {
            kiss.append(fesc);
            kiss.append(tfend);
        }
        else if (c == fesc)
        {
            kiss.append(fesc);
            kiss.append(tfesc);
        }
        else
        {
            kiss.append(c);
        }
    }

    kiss.append(fend);
    m_framesFile.write(kiss);
    m_framesFile.flush();
}

QString SSDVDemod::autoSaveImage(Image& image, bool complete)
{
    SSDV::ImageDecoder *decoder = image.m_decoder;

    // Don't save fragments
    if (!m_settings.m_autoSave || (!complete && (decoder->getProgress() < 0.25f))) {
        return QString();
    }

    QString path = m_settings.m_autoSavePath;

    if (path.isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/SSDV";
    }

    QDir dir(path);

    if (!dir.mkpath("."))
    {
        qWarning() << "SSDVDemod::autoSaveImage: could not create directory" << path;
        return QString();
    }

    QString fileName = QString("SSDV_%1_%2_%3%4.jpg")
        .arg(decoder->getCallsign().isEmpty() ? "unknown" : decoder->getCallsign())
        .arg(decoder->getFirstPacketTime().toString("yyyyMMdd_HHmmss"))
        .arg(decoder->getImageId(), 3, 10, QChar('0'))
        .arg(complete ? "" : "_partial");
    QFile file(dir.filePath(fileName));

    if (!file.open(QIODevice::WriteOnly) || (file.write(decoder->jpeg()) < 0))
    {
        qWarning() << "SSDVDemod::autoSaveImage: could not save image to" << file.fileName();
        return QString();
    }

    image.m_saved = true;
    m_lastSavedFile = file.fileName();
    qDebug() << "SSDVDemod::autoSaveImage: saved" << m_lastSavedFile;

    return m_lastSavedFile;
}

bool SSDVDemod::saveImage(const QString& fileName)
{
    if (m_images.isEmpty()) {
        return false;
    }

    QByteArray jpeg = m_images.first().m_decoder->jpeg();
    QFile file(fileName);

    if (fileName.endsWith(".jpg", Qt::CaseInsensitive) || fileName.endsWith(".jpeg", Qt::CaseInsensitive))
    {
        if (!file.open(QIODevice::WriteOnly) || (file.write(jpeg) < 0)) {
            return false;
        }
    }
    else
    {
        QImage image;

        if (!image.loadFromData(jpeg, "JPEG") || !image.save(fileName)) {
            return false;
        }
    }

    m_lastSavedFile = fileName;
    return true;
}

void SSDVDemod::clearImages()
{
    for (auto& image : m_images)
    {
        if (!image.m_saved) {
            autoSaveImage(image, false);
        }

        delete image.m_decoder;
    }

    m_images.clear();
    m_frameParser.reset();
}

void SSDVDemod::setCenterFrequency(qint64 frequency)
{
    SSDVDemodSettings settings = m_settings;
    settings.m_inputFrequencyOffset = frequency;
    applySettings(QStringList("inputFrequencyOffset"), settings, false);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureSSDVDemod *msgToGUI = MsgConfigureSSDVDemod::create(QStringList("inputFrequencyOffset"), settings, false);
        m_guiMessageQueue->push(msgToGUI);
    }
}

void SSDVDemod::applySettings(const QStringList& settingsKeys, const SSDVDemodSettings& settings, bool force)
{
    qDebug() << "SSDVDemod::applySettings:" << settings.getDebugString(settingsKeys, force);

    if (settingsKeys.contains("streamIndex") && (m_settings.m_streamIndex != settings.m_streamIndex))
    {
        if (m_deviceAPI->getSampleMIMO()) // change of stream is possible for MIMO devices only
        {
            m_deviceAPI->removeChannelSinkAPI(this);
            m_deviceAPI->removeChannelSink(this, m_settings.m_streamIndex);
            m_deviceAPI->addChannelSink(this, settings.m_streamIndex);
            m_deviceAPI->addChannelSinkAPI(this);
            m_settings.m_streamIndex = settings.m_streamIndex; // make sure ChannelAPI::getStreamIndex() is consistent
            emit streamIndexChanged(settings.m_streamIndex);
        }
    }

    if ((settingsKeys.contains("baudRate") && (m_settings.m_baudRate != settings.m_baudRate)) || force) {
        m_scopeVis.setLiveRate(settings.m_baudRate);
    }

    if (settingsKeys.contains("packetFormat") || settingsKeys.contains("framing") || force) {
        m_frameParser.setFormat(settings.m_packetFormat, settings.m_framing);
    }

    if (settingsKeys.contains("framesFilename") || (settingsKeys.contains("saveFrames") && !settings.m_saveFrames)) {
        m_framesFile.close();
    }

    if (m_running)
    {
        SSDVDemodBaseband::MsgConfigureSSDVDemodBaseband *msg = SSDVDemodBaseband::MsgConfigureSSDVDemodBaseband::create(settingsKeys, settings, force);
        m_basebandSink->getInputMessageQueue()->push(msg);
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

    if (pipes.size() > 0) {
        sendChannelSettings(pipes, settingsKeys, settings, force);
    }

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }
}

QByteArray SSDVDemod::serialize() const
{
    return m_settings.serialize();
}

bool SSDVDemod::deserialize(const QByteArray& data)
{
    bool success = true;

    if (!m_settings.deserialize(data))
    {
        m_settings.resetToDefaults();
        success = false;
    }

    MsgConfigureSSDVDemod *msg = MsgConfigureSSDVDemod::create(QStringList(), m_settings, true);
    m_inputMessageQueue.push(msg);

    return success;
}

void SSDVDemod::sendSampleRateToDemodAnalyzer()
{
    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(this, "reportdemod", pipes);

    for (const auto& pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);
        MainCore::MsgChannelDemodReport *msg = MainCore::MsgChannelDemodReport::create(
            this,
            m_settings.m_baudRate * SSDVDemodSettings::m_samplesPerSymbol
        );
        messageQueue->push(msg);
    }
}

int SSDVDemod::webapiSettingsGet(
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setSsdvDemodSettings(new SWGSDRangel::SWGSSDVDemodSettings());
    response.getSsdvDemodSettings()->init();
    webapiFormatChannelSettings(response, m_settings);
    return 200;
}

int SSDVDemod::webapiWorkspaceGet(
        SWGSDRangel::SWGWorkspaceInfo& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setIndex(m_settings.m_workspaceIndex);
    return 200;
}

int SSDVDemod::webapiSettingsPutPatch(
        bool force,
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage)
{
    (void) errorMessage;
    SSDVDemodSettings settings = m_settings;
    webapiUpdateChannelSettings(settings, channelSettingsKeys, response);

    MsgConfigureSSDVDemod *msg = MsgConfigureSSDVDemod::create(channelSettingsKeys, settings, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureSSDVDemod *msgToGUI = MsgConfigureSSDVDemod::create(channelSettingsKeys, settings, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatChannelSettings(response, settings);

    return 200;
}

void SSDVDemod::webapiUpdateChannelSettings(
        SSDVDemodSettings& settings,
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings& response)
{
    SWGSDRangel::SWGSSDVDemodSettings *swgSettings = response.getSsdvDemodSettings();

    if (channelSettingsKeys.contains("inputFrequencyOffset")) {
        settings.m_inputFrequencyOffset = swgSettings->getInputFrequencyOffset();
    }
    if (channelSettingsKeys.contains("baudRate")) {
        settings.m_baudRate = swgSettings->getBaudRate();
    }
    if (channelSettingsKeys.contains("rrcRolloff")) {
        settings.m_rrcRolloff = swgSettings->getRrcRolloff();
    }
    if (channelSettingsKeys.contains("pllBandwidth")) {
        settings.m_pllBandwidth = swgSettings->getPllBandwidth();
    }
    if (channelSettingsKeys.contains("afc")) {
        settings.m_afc = swgSettings->getAfc() != 0;
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
    if (channelSettingsKeys.contains("autoSave")) {
        settings.m_autoSave = swgSettings->getAutoSave() != 0;
    }
    if (channelSettingsKeys.contains("autoSavePath")) {
        settings.m_autoSavePath = *swgSettings->getAutoSavePath();
    }
    if (channelSettingsKeys.contains("saveFrames")) {
        settings.m_saveFrames = swgSettings->getSaveFrames() != 0;
    }
    if (channelSettingsKeys.contains("framesFilename")) {
        settings.m_framesFilename = *swgSettings->getFramesFilename();
    }
    if (channelSettingsKeys.contains("modulation")) {
        settings.m_modulation = swgSettings->getModulation();
    }
    if (channelSettingsKeys.contains("fskDeviation")) {
        settings.m_fskDeviation = swgSettings->getFskDeviation();
    }
    if (channelSettingsKeys.contains("framing")) {
        settings.m_framing = swgSettings->getFraming();
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

int SSDVDemod::webapiReportGet(
        SWGSDRangel::SWGChannelReport& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setSsdvDemodReport(new SWGSDRangel::SWGSSDVDemodReport());
    response.getSsdvDemodReport()->init();
    webapiFormatChannelReport(response);
    return 200;
}

void SSDVDemod::webapiFormatChannelSettings(SWGSDRangel::SWGChannelSettings& response, const SSDVDemodSettings& settings)
{
    SWGSDRangel::SWGSSDVDemodSettings *swgSettings = response.getSsdvDemodSettings();

    swgSettings->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    swgSettings->setBaudRate(settings.m_baudRate);
    swgSettings->setRrcRolloff(settings.m_rrcRolloff);
    swgSettings->setPllBandwidth(settings.m_pllBandwidth);
    swgSettings->setAfc(settings.m_afc ? 1 : 0);
    swgSettings->setFrameSize(settings.m_frameSize);
    swgSettings->setRs(settings.m_rs ? 1 : 0);
    swgSettings->setDualBasis(settings.m_dualBasis ? 1 : 0);
    swgSettings->setRandomizer(settings.m_randomizer ? 1 : 0);
    swgSettings->setDifferential(settings.m_differential ? 1 : 0);
    swgSettings->setConvolutional(settings.m_convolutional ? 1 : 0);
    swgSettings->setPacketFormat(settings.m_packetFormat);
    swgSettings->setAutoSave(settings.m_autoSave ? 1 : 0);
    if (swgSettings->getAutoSavePath()) {
        *swgSettings->getAutoSavePath() = settings.m_autoSavePath;
    } else {
        swgSettings->setAutoSavePath(new QString(settings.m_autoSavePath));
    }
    swgSettings->setSaveFrames(settings.m_saveFrames ? 1 : 0);
    if (swgSettings->getFramesFilename()) {
        *swgSettings->getFramesFilename() = settings.m_framesFilename;
    } else {
        swgSettings->setFramesFilename(new QString(settings.m_framesFilename));
    }
    swgSettings->setModulation(settings.m_modulation);
    swgSettings->setFskDeviation(settings.m_fskDeviation);
    swgSettings->setFraming(settings.m_framing);
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

void SSDVDemod::webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response)
{
    SWGSDRangel::SWGSSDVDemodReport *swgReport = response.getSsdvDemodReport();
    double magsqAvg, magsqPeak;
    int nbMagsqSamples;
    getMagSqLevels(magsqAvg, magsqPeak, nbMagsqSamples);

    swgReport->setChannelPowerDb(CalcDb::dbPower(magsqAvg));

    if (m_running) {
        swgReport->setChannelSampleRate(m_basebandSink->getChannelSampleRate());
    }

    swgReport->setLocked(isLocked() ? 1 : 0);
    swgReport->setFrequencyOffset(getFrequencyOffset());
    swgReport->setEvm(getEvm());
    swgReport->setFramesReceived(m_framesReceived);
    swgReport->setFramesBad(getFramesBad());
    swgReport->setPacketsReceived(m_packetsReceived);
    swgReport->setImagesReceived(m_imagesReceived);

    if (!m_images.isEmpty())
    {
        SSDV::ImageDecoder *decoder = m_images.first().m_decoder;
        *swgReport->getCallsign() = decoder->getCallsign();
        swgReport->setImageId(decoder->getImageId());
        swgReport->setImagePacketsReceived(decoder->getPacketsReceived());
        swgReport->setImagePacketCount(decoder->getPacketCount());
        swgReport->setImageMissingPackets(decoder->getMissingPackets());
    }
    else
    {
        swgReport->setImageId(-1);
    }

    *swgReport->getLastSavedFile() = m_lastSavedFile;
}

void SSDVDemod::webapiReverseSendSettings(const QList<QString>& channelSettingsKeys, const SSDVDemodSettings& settings, bool force)
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

void SSDVDemod::sendChannelSettings(
    const QList<ObjectPipe*>& pipes,
    const QList<QString>& channelSettingsKeys,
    const SSDVDemodSettings& settings,
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

void SSDVDemod::webapiFormatChannelSettings(
        const QList<QString>& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings *swgChannelSettings,
        const SSDVDemodSettings& settings,
        bool force
)
{
    swgChannelSettings->setDirection(0); // Single sink (Rx)
    swgChannelSettings->setOriginatorChannelIndex(getIndexInDeviceSet());
    swgChannelSettings->setOriginatorDeviceSetIndex(getDeviceSetIndex());
    swgChannelSettings->setChannelType(new QString(m_channelId));
    swgChannelSettings->setSsdvDemodSettings(new SWGSDRangel::SWGSSDVDemodSettings());
    SWGSDRangel::SWGSSDVDemodSettings *swgSettings = swgChannelSettings->getSsdvDemodSettings();

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
    if (channelSettingsKeys.contains("pllBandwidth") || force) {
        swgSettings->setPllBandwidth(settings.m_pllBandwidth);
    }
    if (channelSettingsKeys.contains("afc") || force) {
        swgSettings->setAfc(settings.m_afc ? 1 : 0);
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
    if (channelSettingsKeys.contains("autoSave") || force) {
        swgSettings->setAutoSave(settings.m_autoSave ? 1 : 0);
    }
    if (channelSettingsKeys.contains("autoSavePath") || force) {
        swgSettings->setAutoSavePath(new QString(settings.m_autoSavePath));
    }
    if (channelSettingsKeys.contains("saveFrames") || force) {
        swgSettings->setSaveFrames(settings.m_saveFrames ? 1 : 0);
    }
    if (channelSettingsKeys.contains("framesFilename") || force) {
        swgSettings->setFramesFilename(new QString(settings.m_framesFilename));
    }
    if (channelSettingsKeys.contains("modulation") || force) {
        swgSettings->setModulation(settings.m_modulation);
    }
    if (channelSettingsKeys.contains("fskDeviation") || force) {
        swgSettings->setFskDeviation(settings.m_fskDeviation);
    }
    if (channelSettingsKeys.contains("framing") || force) {
        swgSettings->setFraming(settings.m_framing);
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

void SSDVDemod::networkManagerFinished(QNetworkReply *reply)
{
    QNetworkReply::NetworkError replyError = reply->error();

    if (replyError)
    {
        qWarning() << "SSDVDemod::networkManagerFinished:"
                << " error(" << (int) replyError
                << "): " << replyError
                << ": " << reply->errorString();
    }
    else
    {
        QString answer = reply->readAll();
        answer.chop(1); // remove last \n
        qDebug("SSDVDemod::networkManagerFinished: reply:\n%s", answer.toStdString().c_str());
    }

    reply->deleteLater();
}

void SSDVDemod::handleIndexInDeviceSetChanged(int index)
{
    if (!m_running || (index < 0)) {
        return;
    }

    QString fifoLabel = QString("%1 [%2:%3]")
        .arg(m_channelId)
        .arg(m_deviceAPI->getDeviceSetIndex())
        .arg(index);
    m_basebandSink->setFifoLabel(fifoLabel);
}

int SSDVDemod::getChannelSampleRate() const
{
    return m_basebandSink ? m_basebandSink->getChannelSampleRate() : 0;
}
