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
#include <QPainter>
#include <QStandardPaths>
#include <QThread>

#include "SWGChannelSettings.h"
#include "SWGWorkspaceInfo.h"
#include "SWGSSTVDemodSettings.h"
#include "SWGChannelReport.h"
#include "SWGSSTVDemodReport.h"

#include "dsp/dspcommands.h"
#include "device/deviceapi.h"
#include "settings/serializable.h"
#include "util/db.h"
#include "maincore.h"

#include "sstvmodes.h"
#include "sstvdemodreport.h"
#include "sstvdemod.h"

MESSAGE_CLASS_DEFINITION(SSTVDemod::MsgConfigureSSTVDemod, Message)
MESSAGE_CLASS_DEFINITION(SSTVDemod::MsgClear, Message)

const char* const SSTVDemod::m_channelIdURI = "sdrangel.channel.sstvdemod";
const char* const SSTVDemod::m_channelId = "SSTVDemod";

SSTVDemod::SSTVDemod(DeviceAPI *deviceAPI) :
    ChannelAPI(m_channelIdURI, ChannelAPI::StreamSingleSink),
    m_deviceAPI(deviceAPI),
    m_thread(nullptr),
    m_basebandSink(nullptr),
    m_running(false),
    m_basebandSampleRate(0),
    m_centerFrequency(0),
    m_receiving(false),
    m_modeIndex(-1),
    m_visCode(-1),
    m_rowsDone(0),
    m_clockErrorPpm(0.0),
    m_imagesDecoded(0)
{
    qDebug("SSTVDemod::SSTVDemod");
    setObjectName(m_channelId);
    applySettings(QStringList(), m_settings, true);

    m_deviceAPI->addChannelSink(this);
    m_deviceAPI->addChannelSinkAPI(this);

    m_networkManager = new QNetworkAccessManager();
    QObject::connect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &SSTVDemod::networkManagerFinished
    );
    QObject::connect(
        this,
        &ChannelAPI::indexInDeviceSetChanged,
        this,
        &SSTVDemod::handleIndexInDeviceSetChanged
    );
}

SSTVDemod::~SSTVDemod()
{
    QObject::disconnect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &SSTVDemod::networkManagerFinished
    );
    delete m_networkManager;
    m_deviceAPI->removeChannelSinkAPI(this);
    m_deviceAPI->removeChannelSink(this, true);

    stop();
}

void SSTVDemod::setDeviceAPI(DeviceAPI *deviceAPI)
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

uint32_t SSTVDemod::getNumberOfDeviceStreams() const
{
    return m_deviceAPI->getNbSourceStreams();
}

void SSTVDemod::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, bool firstOfBurst)
{
    (void) firstOfBurst;
    m_basebandSink->feed(begin, end);
}

void SSTVDemod::start()
{
    if (m_running) {
        return;
    }

    qDebug() << "SSTVDemod::start";
    m_thread = new QThread();
    m_thread->setObjectName("SSTVDemod");
    m_basebandSink = new SSTVDemodBaseband();
    m_basebandSink->setFifoLabel(QString("%1 [%2:%3]")
        .arg(m_channelId)
        .arg(m_deviceAPI->getDeviceSetIndex())
        .arg(getIndexInDeviceSet())
    );
    m_basebandSink->setChannel(this);
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

    SSTVDemodBaseband::MsgConfigureSSTVDemodBaseband *msg = SSTVDemodBaseband::MsgConfigureSSTVDemodBaseband::create(QStringList(), m_settings, true);
    m_basebandSink->getInputMessageQueue()->push(msg);

    m_running = true;
}

void SSTVDemod::stop()
{
    if (!m_running) {
        return;
    }

    qDebug() << "SSTVDemod::stop";
    m_running = false;
    m_thread->quit();
    m_thread->wait();
    m_basebandSink = nullptr; // Freed by deleteLater when the thread finished

    if (m_receiving)
    {
        m_receiving = false;
        autoSaveImage(false);
    }
}

bool SSTVDemod::handleMessage(const Message& cmd)
{
    if (MsgConfigureSSTVDemod::match(cmd))
    {
        const MsgConfigureSSTVDemod& cfg = (const MsgConfigureSSTVDemod&) cmd;
        qDebug() << "SSTVDemod::handleMessage: MsgConfigureSSTVDemod";
        applySettings(cfg.getSettingsKeys(), cfg.getSettings(), cfg.getForce());

        return true;
    }
    else if (MsgClear::match(cmd))
    {
        m_receiving = false;
        m_rowsDone = 0;
        m_image = QImage();

        if (m_running) {
            m_basebandSink->getInputMessageQueue()->push(SSTVDemodBaseband::MsgResetDecoder::create());
        }

        return true;
    }
    else if (SSTVDemodReport::MsgImageStarted::match(cmd))
    {
        const SSTVDemodReport::MsgImageStarted& report = (const SSTVDemodReport::MsgImageStarted&) cmd;
        const SSTVMode& mode = SSTVMode::getMode(report.getModeIndex());
        m_receiving = true;
        m_modeIndex = report.getModeIndex();
        m_visCode = report.getVisCode();
        m_rowsDone = 0;
        m_clockErrorPpm = 0.0;
        m_image = QImage(mode.m_width, mode.m_height, QImage::Format_RGB32);
        m_image.fill(Qt::black);

        if (getMessageQueueToGUI()) {
            getMessageQueueToGUI()->push(SSTVDemodReport::MsgImageStarted::create(report.getModeIndex(), report.getVisCode()));
        }

        return true;
    }
    else if (SSTVDemodReport::MsgLines::match(cmd))
    {
        const SSTVDemodReport::MsgLines& report = (const SSTVDemodReport::MsgLines&) cmd;

        if (!m_image.isNull())
        {
            QPainter painter(&m_image);
            painter.drawImage(0, report.getFirstRow(), report.getRows());
        }

        m_rowsDone = report.getRowsDone();
        m_clockErrorPpm = report.getClockErrorPpm();

        if (getMessageQueueToGUI())
        {
            getMessageQueueToGUI()->push(SSTVDemodReport::MsgLines::create(
                report.getFirstRow(), report.getRows(), report.getRowsDone(), report.getClockErrorPpm()));
        }

        return true;
    }
    else if (SSTVDemodReport::MsgImageFinished::match(cmd))
    {
        const SSTVDemodReport::MsgImageFinished& report = (const SSTVDemodReport::MsgImageFinished&) cmd;
        m_image = report.getImage();
        m_receiving = false;
        m_imagesDecoded++;
        autoSaveImage(report.getComplete());

        if (getMessageQueueToGUI()) {
            getMessageQueueToGUI()->push(SSTVDemodReport::MsgImageFinished::create(report.getImage(), report.getComplete()));
        }

        return true;
    }
    else if (DSPSignalNotification::match(cmd))
    {
        qDebug() << "SSTVDemod::handleMessage: DSPSignalNotification";
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
        qDebug() << "SSTVDemod::handleMessage: MsgChannelDemodQuery";
        sendSampleRateToDemodAnalyzer();

        return true;
    }
    else
    {
        return false;
    }
}

bool SSTVDemod::saveImage(const QString& fileName)
{
    if (m_image.isNull()) {
        return false;
    }

    // Only save the rows received
    const QImage image = m_receiving ? m_image.copy(0, 0, m_image.width(), std::max(1, m_rowsDone)) : m_image;

    if (!image.save(fileName))
    {
        qWarning() << "SSTVDemod::saveImage: could not save image to" << fileName;
        return false;
    }

    m_lastSavedFile = fileName;
    return true;
}

void SSTVDemod::autoSaveImage(bool complete)
{
    const SSTVMode& mode = SSTVMode::getMode(m_modeIndex);

    // Don't save fragments
    if (!m_settings.m_autoSave || m_image.isNull() || (m_rowsDone < mode.m_height / 4)) {
        return;
    }

    QString path = m_settings.m_autoSavePath;

    if (path.isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/SSTV";
    }

    QDir dir(path);

    if (!dir.mkpath("."))
    {
        qWarning() << "SSTVDemod::autoSaveImage: could not create directory" << path;
        return;
    }

    QString modeName = QString(mode.m_name).remove(' ');
    QString fileName = QString("SSTV_%1_%2%3.png")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"))
        .arg(modeName)
        .arg(complete ? "" : "_partial");
    const QImage image = complete ? m_image : m_image.copy(0, 0, m_image.width(), m_rowsDone);

    if (image.save(dir.filePath(fileName)))
    {
        m_lastSavedFile = dir.filePath(fileName);
        qDebug() << "SSTVDemod::autoSaveImage: saved" << m_lastSavedFile;
    }
    else
    {
        qWarning() << "SSTVDemod::autoSaveImage: could not save image to" << dir.filePath(fileName);
    }
}

void SSTVDemod::setCenterFrequency(qint64 frequency)
{
    SSTVDemodSettings settings = m_settings;
    settings.m_inputFrequencyOffset = frequency;
    applySettings(QStringList("inputFrequencyOffset"), settings, false);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureSSTVDemod *msgToGUI = MsgConfigureSSTVDemod::create(QStringList("inputFrequencyOffset"), settings, false);
        m_guiMessageQueue->push(msgToGUI);
    }
}

void SSTVDemod::applySettings(const QStringList& settingsKeys, const SSTVDemodSettings& settings, bool force)
{
    qDebug() << "SSTVDemod::applySettings:" << settings.getDebugString(settingsKeys, force);

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

    if (m_running)
    {
        SSTVDemodBaseband::MsgConfigureSSTVDemodBaseband *msg = SSTVDemodBaseband::MsgConfigureSSTVDemodBaseband::create(settingsKeys, settings, force);
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

QByteArray SSTVDemod::serialize() const
{
    return m_settings.serialize();
}

bool SSTVDemod::deserialize(const QByteArray& data)
{
    bool success = true;

    if (!m_settings.deserialize(data))
    {
        m_settings.resetToDefaults();
        success = false;
    }

    MsgConfigureSSTVDemod *msg = MsgConfigureSSTVDemod::create(QStringList(), m_settings, true);
    m_inputMessageQueue.push(msg);

    return success;
}

void SSTVDemod::sendSampleRateToDemodAnalyzer()
{
    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(this, "reportdemod", pipes);

    for (const auto& pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);
        MainCore::MsgChannelDemodReport *msg = MainCore::MsgChannelDemodReport::create(
            this,
            getAudioSampleRate()
        );
        messageQueue->push(msg);
    }
}

int SSTVDemod::webapiSettingsGet(
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setSstvDemodSettings(new SWGSDRangel::SWGSSTVDemodSettings());
    response.getSstvDemodSettings()->init();
    webapiFormatChannelSettings(response, m_settings);
    return 200;
}

int SSTVDemod::webapiWorkspaceGet(
        SWGSDRangel::SWGWorkspaceInfo& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setIndex(m_settings.m_workspaceIndex);
    return 200;
}

int SSTVDemod::webapiSettingsPutPatch(
        bool force,
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage)
{
    (void) errorMessage;
    SSTVDemodSettings settings = m_settings;
    webapiUpdateChannelSettings(settings, channelSettingsKeys, response);

    MsgConfigureSSTVDemod *msg = MsgConfigureSSTVDemod::create(channelSettingsKeys, settings, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureSSTVDemod *msgToGUI = MsgConfigureSSTVDemod::create(channelSettingsKeys, settings, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatChannelSettings(response, settings);

    return 200;
}

void SSTVDemod::webapiUpdateChannelSettings(
        SSTVDemodSettings& settings,
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings& response)
{
    SWGSDRangel::SWGSSTVDemodSettings *swgSettings = response.getSstvDemodSettings();

    if (channelSettingsKeys.contains("inputFrequencyOffset")) {
        settings.m_inputFrequencyOffset = swgSettings->getInputFrequencyOffset();
    }
    if (channelSettingsKeys.contains("rfBandwidth")) {
        settings.m_rfBandwidth = swgSettings->getRfBandwidth();
    }
    if (channelSettingsKeys.contains("fmDeviation")) {
        settings.m_fmDeviation = swgSettings->getFmDeviation();
    }
    if (channelSettingsKeys.contains("autoMode")) {
        settings.m_autoMode = swgSettings->getAutoMode() != 0;
    }
    if (channelSettingsKeys.contains("mode")) {
        settings.m_mode = std::max(0, std::min((int) SSTVMode::NbModes - 1, swgSettings->getMode()));
    }
    if (channelSettingsKeys.contains("autoSave")) {
        settings.m_autoSave = swgSettings->getAutoSave() != 0;
    }
    if (channelSettingsKeys.contains("autoSavePath")) {
        settings.m_autoSavePath = *swgSettings->getAutoSavePath();
    }
    if (channelSettingsKeys.contains("volume")) {
        settings.m_volume = swgSettings->getVolume();
    }
    if (channelSettingsKeys.contains("audioMute")) {
        settings.m_audioMute = swgSettings->getAudioMute() != 0;
    }
    if (channelSettingsKeys.contains("audioDeviceName")) {
        settings.m_audioDeviceName = *swgSettings->getAudioDeviceName();
    }
    if (channelSettingsKeys.contains("rgbColor")) {
        settings.m_rgbColor = swgSettings->getRgbColor();
    }
    if (channelSettingsKeys.contains("title")) {
        settings.m_title = *swgSettings->getTitle();
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
    if (settings.m_channelMarker && channelSettingsKeys.contains("channelMarker")) {
        settings.m_channelMarker->updateFrom(channelSettingsKeys, swgSettings->getChannelMarker());
    }
    if (settings.m_rollupState && channelSettingsKeys.contains("rollupState")) {
        settings.m_rollupState->updateFrom(channelSettingsKeys, swgSettings->getRollupState());
    }
}

int SSTVDemod::webapiReportGet(
        SWGSDRangel::SWGChannelReport& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setSstvDemodReport(new SWGSDRangel::SWGSSTVDemodReport());
    response.getSstvDemodReport()->init();
    webapiFormatChannelReport(response);
    return 200;
}

void SSTVDemod::webapiFormatChannelSettings(SWGSDRangel::SWGChannelSettings& response, const SSTVDemodSettings& settings)
{
    SWGSDRangel::SWGSSTVDemodSettings *swgSettings = response.getSstvDemodSettings();

    swgSettings->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    swgSettings->setRfBandwidth(settings.m_rfBandwidth);
    swgSettings->setFmDeviation(settings.m_fmDeviation);
    swgSettings->setAutoMode(settings.m_autoMode ? 1 : 0);
    swgSettings->setMode(settings.m_mode);
    swgSettings->setAutoSave(settings.m_autoSave ? 1 : 0);

    if (swgSettings->getAutoSavePath()) {
        *swgSettings->getAutoSavePath() = settings.m_autoSavePath;
    } else {
        swgSettings->setAutoSavePath(new QString(settings.m_autoSavePath));
    }

    swgSettings->setVolume(settings.m_volume);
    swgSettings->setAudioMute(settings.m_audioMute ? 1 : 0);

    if (swgSettings->getAudioDeviceName()) {
        *swgSettings->getAudioDeviceName() = settings.m_audioDeviceName;
    } else {
        swgSettings->setAudioDeviceName(new QString(settings.m_audioDeviceName));
    }

    swgSettings->setRgbColor(settings.m_rgbColor);

    if (swgSettings->getTitle()) {
        *swgSettings->getTitle() = settings.m_title;
    } else {
        swgSettings->setTitle(new QString(settings.m_title));
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

    if (settings.m_channelMarker)
    {
        if (swgSettings->getChannelMarker())
        {
            settings.m_channelMarker->formatTo(swgSettings->getChannelMarker());
        }
        else
        {
            SWGSDRangel::SWGChannelMarker *swgChannelMarker = new SWGSDRangel::SWGChannelMarker();
            settings.m_channelMarker->formatTo(swgChannelMarker);
            swgSettings->setChannelMarker(swgChannelMarker);
        }
    }

    if (settings.m_rollupState)
    {
        if (swgSettings->getRollupState())
        {
            settings.m_rollupState->formatTo(swgSettings->getRollupState());
        }
        else
        {
            SWGSDRangel::SWGRollupState *swgRollupState = new SWGSDRangel::SWGRollupState();
            settings.m_rollupState->formatTo(swgRollupState);
            swgSettings->setRollupState(swgRollupState);
        }
    }
}

void SSTVDemod::webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response)
{
    SWGSDRangel::SWGSSTVDemodReport *swgReport = response.getSstvDemodReport();
    double magsqAvg, magsqPeak;
    int nbMagsqSamples;
    getMagSqLevels(magsqAvg, magsqPeak, nbMagsqSamples);

    swgReport->setChannelPowerDb(CalcDb::dbPower(magsqAvg));

    if (m_running)
    {
        swgReport->setAudioSampleRate(m_basebandSink->getAudioSampleRate());
        swgReport->setChannelSampleRate(m_basebandSink->getChannelSampleRate());
    }

    swgReport->setReceiving(m_receiving ? 1 : 0);
    *swgReport->getMode() = m_modeIndex >= 0 ? SSTVMode::getMode(m_modeIndex).m_name : "";
    swgReport->setVisCode(m_visCode);
    swgReport->setRowsDone(m_rowsDone);
    swgReport->setHeight(m_image.isNull() ? 0 : m_image.height());
    swgReport->setClockErrorPpm(m_clockErrorPpm);
    swgReport->setImagesDecoded(m_imagesDecoded);
    *swgReport->getLastSavedFile() = m_lastSavedFile;
}

void SSTVDemod::webapiReverseSendSettings(const QList<QString>& channelSettingsKeys, const SSTVDemodSettings& settings, bool force)
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

void SSTVDemod::sendChannelSettings(
    const QList<ObjectPipe*>& pipes,
    const QList<QString>& channelSettingsKeys,
    const SSTVDemodSettings& settings,
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

void SSTVDemod::webapiFormatChannelSettings(
        const QList<QString>& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings *swgChannelSettings,
        const SSTVDemodSettings& settings,
        bool force
)
{
    swgChannelSettings->setDirection(0); // Single sink (Rx)
    swgChannelSettings->setOriginatorChannelIndex(getIndexInDeviceSet());
    swgChannelSettings->setOriginatorDeviceSetIndex(getDeviceSetIndex());
    swgChannelSettings->setChannelType(new QString(m_channelId));
    swgChannelSettings->setSstvDemodSettings(new SWGSDRangel::SWGSSTVDemodSettings());
    SWGSDRangel::SWGSSTVDemodSettings *swgSettings = swgChannelSettings->getSstvDemodSettings();

    // transfer data that has been modified. When force is on transfer all data except reverse API data

    if (channelSettingsKeys.contains("inputFrequencyOffset") || force) {
        swgSettings->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    }
    if (channelSettingsKeys.contains("rfBandwidth") || force) {
        swgSettings->setRfBandwidth(settings.m_rfBandwidth);
    }
    if (channelSettingsKeys.contains("fmDeviation") || force) {
        swgSettings->setFmDeviation(settings.m_fmDeviation);
    }
    if (channelSettingsKeys.contains("autoMode") || force) {
        swgSettings->setAutoMode(settings.m_autoMode ? 1 : 0);
    }
    if (channelSettingsKeys.contains("mode") || force) {
        swgSettings->setMode(settings.m_mode);
    }
    if (channelSettingsKeys.contains("autoSave") || force) {
        swgSettings->setAutoSave(settings.m_autoSave ? 1 : 0);
    }
    if (channelSettingsKeys.contains("autoSavePath") || force) {
        swgSettings->setAutoSavePath(new QString(settings.m_autoSavePath));
    }
    if (channelSettingsKeys.contains("volume") || force) {
        swgSettings->setVolume(settings.m_volume);
    }
    if (channelSettingsKeys.contains("audioMute") || force) {
        swgSettings->setAudioMute(settings.m_audioMute ? 1 : 0);
    }
    if (channelSettingsKeys.contains("audioDeviceName") || force) {
        swgSettings->setAudioDeviceName(new QString(settings.m_audioDeviceName));
    }
    if (channelSettingsKeys.contains("rgbColor") || force) {
        swgSettings->setRgbColor(settings.m_rgbColor);
    }
    if (channelSettingsKeys.contains("title") || force) {
        swgSettings->setTitle(new QString(settings.m_title));
    }
    if (channelSettingsKeys.contains("streamIndex") || force) {
        swgSettings->setStreamIndex(settings.m_streamIndex);
    }

    if (settings.m_channelMarker && (channelSettingsKeys.contains("channelMarker") || force))
    {
        SWGSDRangel::SWGChannelMarker *swgChannelMarker = new SWGSDRangel::SWGChannelMarker();
        settings.m_channelMarker->formatTo(swgChannelMarker);
        swgSettings->setChannelMarker(swgChannelMarker);
    }

    if (settings.m_rollupState && (channelSettingsKeys.contains("rollupState") || force))
    {
        SWGSDRangel::SWGRollupState *swgRollupState = new SWGSDRangel::SWGRollupState();
        settings.m_rollupState->formatTo(swgRollupState);
        swgSettings->setRollupState(swgRollupState);
    }
}

void SSTVDemod::networkManagerFinished(QNetworkReply *reply)
{
    QNetworkReply::NetworkError replyError = reply->error();

    if (replyError)
    {
        qWarning() << "SSTVDemod::networkManagerFinished:"
                << " error(" << (int) replyError
                << "): " << replyError
                << ": " << reply->errorString();
    }
    else
    {
        QString answer = reply->readAll();
        answer.chop(1); // remove last \n
        qDebug("SSTVDemod::networkManagerFinished: reply:\n%s", answer.toStdString().c_str());
    }

    reply->deleteLater();
}

void SSTVDemod::handleIndexInDeviceSetChanged(int index)
{
    if (!m_running || (index < 0)) {
        return;
    }

    QString fifoLabel = QString("%1 [%2:%3]")
        .arg(m_channelId)
        .arg(m_deviceAPI->getDeviceSetIndex())
        .arg(index);
    m_basebandSink->setFifoLabel(fifoLabel);
    m_basebandSink->setAudioFifoLabel(fifoLabel);
}

int SSTVDemod::getChannelSampleRate() const
{
    return m_basebandSink ? m_basebandSink->getChannelSampleRate() : 0;
}
