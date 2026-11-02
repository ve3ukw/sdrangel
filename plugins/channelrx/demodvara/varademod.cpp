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

#include <algorithm>
#include <cmath>

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QBuffer>
#include <QThread>

#include "SWGChannelSettings.h"
#include "SWGWorkspaceInfo.h"
#include "SWGVARADemodSettings.h"
#include "SWGChannelReport.h"
#include "SWGVARADemodReport.h"

#include "dsp/dspcommands.h"
#include "device/deviceapi.h"
#include "settings/serializable.h"
#include "util/db.h"
#include "maincore.h"

#include "varademod.h"

MESSAGE_CLASS_DEFINITION(VARADemod::MsgConfigureVARADemod, Message)
MESSAGE_CLASS_DEFINITION(VARADemod::MsgLog, Message)

const char* const VARADemod::m_channelIdURI = "sdrangel.channel.varademod";
const char* const VARADemod::m_channelId = "VARADemod";

VARADemod::VARADemod(DeviceAPI *deviceAPI) :
    ChannelAPI(m_channelIdURI, ChannelAPI::StreamSingleSink),
    m_deviceAPI(deviceAPI),
    m_thread(nullptr),
    m_basebandSink(nullptr),
    m_running(false),
    m_basebandSampleRate(0),
    m_centerFrequency(0),
    m_engine(new VARAEngine())
{
    qDebug("VARADemod::VARADemod");
    setObjectName(m_channelId);
    m_engine->setLogCallback([this](const QDateTime& dateTime, const QString& line) { logLine(dateTime, line); });
    applySettings(QStringList(), m_settings, true);

    m_deviceAPI->addChannelSink(this);
    m_deviceAPI->addChannelSinkAPI(this);

    m_networkManager = new QNetworkAccessManager();
    QObject::connect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &VARADemod::networkManagerFinished
    );
    QObject::connect(
        this,
        &ChannelAPI::indexInDeviceSetChanged,
        this,
        &VARADemod::handleIndexInDeviceSetChanged
    );
}

VARADemod::~VARADemod()
{
    QObject::disconnect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &VARADemod::networkManagerFinished
    );
    delete m_networkManager;
    m_deviceAPI->removeChannelSinkAPI(this);
    m_deviceAPI->removeChannelSink(this, true);

    stop();
    m_engine.reset(); // after the sink, which feeds it, is gone
}

void VARADemod::setDeviceAPI(DeviceAPI *deviceAPI)
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

uint32_t VARADemod::getNumberOfDeviceStreams() const
{
    return m_deviceAPI->getNbSourceStreams();
}

void VARADemod::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, bool firstOfBurst)
{
    (void) firstOfBurst;
    m_basebandSink->feed(begin, end);
}

void VARADemod::start()
{
    if (m_running) {
        return;
    }

    qDebug() << "VARADemod::start";
    m_thread = new QThread();
    m_thread->setObjectName("VARADemod");
    m_basebandSink = new VARADemodBaseband();
    m_basebandSink->setFifoLabel(QString("%1 [%2:%3]")
        .arg(m_channelId)
        .arg(m_deviceAPI->getDeviceSetIndex())
        .arg(getIndexInDeviceSet())
    );
    m_basebandSink->setChannel(this);
    m_basebandSink->setEngine(m_engine.get());
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

    VARADemodBaseband::MsgConfigureVARADemodBaseband *msg = VARADemodBaseband::MsgConfigureVARADemodBaseband::create(QStringList(), m_settings, true);
    m_basebandSink->getInputMessageQueue()->push(msg);

    m_running = true;
}

void VARADemod::stop()
{
    if (!m_running) {
        return;
    }

    qDebug() << "VARADemod::stop";
    m_running = false;
    m_thread->quit();
    m_thread->wait();
    m_basebandSink = nullptr; // Freed by deleteLater when the thread finished
}

bool VARADemod::handleMessage(const Message& cmd)
{
    if (MsgConfigureVARADemod::match(cmd))
    {
        const MsgConfigureVARADemod& cfg = (const MsgConfigureVARADemod&) cmd;
        qDebug() << "VARADemod::handleMessage: MsgConfigureVARADemod";
        applySettings(cfg.getSettingsKeys(), cfg.getSettings(), cfg.getForce());

        return true;
    }
    else if (DSPSignalNotification::match(cmd))
    {
        qDebug() << "VARADemod::handleMessage: DSPSignalNotification";
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
        qDebug() << "VARADemod::handleMessage: MsgChannelDemodQuery";
        sendSampleRateToDemodAnalyzer();

        return true;
    }
    else
    {
        return false;
    }
}

void VARADemod::setCenterFrequency(qint64 frequency)
{
    VARADemodSettings settings = m_settings;
    settings.m_inputFrequencyOffset = frequency;
    applySettings(QStringList("inputFrequencyOffset"), settings, false);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureVARADemod *msgToGUI = MsgConfigureVARADemod::create(QStringList("inputFrequencyOffset"), settings, false);
        m_guiMessageQueue->push(msgToGUI);
    }
}

VARAEngine::Config VARADemod::getEngineConfig(const VARADemodSettings& settings)
{
    VARAEngine::Config config;
    config.m_mode = settings.m_mode;
    config.m_call = settings.m_callsign.trimmed().toUpper();
    config.m_address = settings.m_address;
    config.m_cmdPort = settings.m_cmdPort;
    config.m_kissPort = settings.m_kissEnabled ? settings.m_kissPort : 0;
    config.m_driveDb = settings.m_driveDb;
    config.m_registered = settings.m_registered;
    return config;
}

void VARADemod::applySettings(const QStringList& settingsKeys, const VARADemodSettings& settings, bool force)
{
    qDebug() << "VARADemod::applySettings:" << settings.getDebugString(settingsKeys, force);

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
        VARADemodBaseband::MsgConfigureVARADemodBaseband *msg = VARADemodBaseband::MsgConfigureVARADemodBaseband::create(settingsKeys, settings, force);
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

    // A changed modem setting restarts the modem, which drops any connection, so only on a real change
    static const QStringList modemKeys = {"mode", "callsign", "address", "cmdPort", "kissEnabled", "kissPort", "driveDb", "registered"};

    if (force || std::any_of(modemKeys.begin(), modemKeys.end(), [&](const QString& k) { return settingsKeys.contains(k); })) {
        m_engine->setConfig(getEngineConfig(m_settings));
    }

    if (settingsKeys.contains("rxLagMs") || force) {
        m_engine->setRxLag(m_settings.m_rxLagMs * VARAEngine::m_sampleRate / 1000);
    }

    if (settingsKeys.contains("logEnabled") || settingsKeys.contains("logFilename") || force) {
        openLogFile(m_settings);
    }
}

void VARADemod::openLogFile(const VARADemodSettings& settings)
{
    QMutexLocker lock(&m_logMutex);

    if (m_logFile.isOpen())
    {
        m_logStream.flush();
        m_logFile.close();
    }

    if (settings.m_logEnabled && !settings.m_logFilename.isEmpty())
    {
        m_logFile.setFileName(settings.m_logFilename);

        if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            m_logStream.setDevice(&m_logFile);
        } else {
            qWarning() << "VARADemod::openLogFile: can't open" << settings.m_logFilename << ":" << m_logFile.errorString();
        }
    }
}

void VARADemod::logLine(const QDateTime& dateTime, const QString& line)
{
    {
        QMutexLocker lock(&m_logMutex);

        if (m_logFile.isOpen())
        {
            m_logStream << dateTime.toString("yyyy-MM-dd HH:mm:ss.zzz") << " " << line << "\n";
            m_logStream.flush();
        }
    }

    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(MsgLog::create(dateTime, line));
    }
}

QByteArray VARADemod::serialize() const
{
    return m_settings.serialize();
}

bool VARADemod::deserialize(const QByteArray& data)
{
    bool success = true;

    if (!m_settings.deserialize(data))
    {
        m_settings.resetToDefaults();
        success = false;
    }

    MsgConfigureVARADemod *msg = MsgConfigureVARADemod::create(QStringList(), m_settings, true);
    m_inputMessageQueue.push(msg);

    return success;
}

void VARADemod::sendSampleRateToDemodAnalyzer()
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

int VARADemod::webapiSettingsGet(
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setVaraDemodSettings(new SWGSDRangel::SWGVARADemodSettings());
    response.getVaraDemodSettings()->init();
    webapiFormatChannelSettings(response, m_settings);
    return 200;
}

int VARADemod::webapiWorkspaceGet(
        SWGSDRangel::SWGWorkspaceInfo& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setIndex(m_settings.m_workspaceIndex);
    return 200;
}

int VARADemod::webapiSettingsPutPatch(
        bool force,
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage)
{
    (void) errorMessage;
    VARADemodSettings settings = m_settings;
    webapiUpdateChannelSettings(settings, channelSettingsKeys, response);

    MsgConfigureVARADemod *msg = MsgConfigureVARADemod::create(channelSettingsKeys, settings, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureVARADemod *msgToGUI = MsgConfigureVARADemod::create(channelSettingsKeys, settings, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatChannelSettings(response, settings);

    return 200;
}

void VARADemod::webapiUpdateChannelSettings(
        VARADemodSettings& settings,
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings& response)
{
    SWGSDRangel::SWGVARADemodSettings *swgSettings = response.getVaraDemodSettings();

    if (channelSettingsKeys.contains("inputFrequencyOffset")) {
        settings.m_inputFrequencyOffset = swgSettings->getInputFrequencyOffset();
    }
    if (channelSettingsKeys.contains("mode")) {
        settings.m_mode = swgSettings->getMode();
    }
    if (channelSettingsKeys.contains("callsign")) {
        settings.m_callsign = *swgSettings->getCallsign();
    }
    if (channelSettingsKeys.contains("address")) {
        settings.m_address = *swgSettings->getAddress();
    }
    if (channelSettingsKeys.contains("cmdPort")) {
        settings.m_cmdPort = swgSettings->getCmdPort();
    }
    if (channelSettingsKeys.contains("kissEnabled")) {
        settings.m_kissEnabled = swgSettings->getKissEnabled() != 0;
    }
    if (channelSettingsKeys.contains("kissPort")) {
        settings.m_kissPort = swgSettings->getKissPort();
    }
    if (channelSettingsKeys.contains("driveDb")) {
        settings.m_driveDb = swgSettings->getDriveDb();
    }
    if (channelSettingsKeys.contains("rxLagMs")) {
        settings.m_rxLagMs = swgSettings->getRxLagMs();
    }
    if (channelSettingsKeys.contains("fmDeviation")) {
        settings.m_fmDeviation = swgSettings->getFmDeviation();
    }
    if (channelSettingsKeys.contains("agc")) {
        settings.m_agc = swgSettings->getAgc() != 0;
    }
    if (channelSettingsKeys.contains("rxGainDb")) {
        settings.m_rxGainDb = swgSettings->getRxGainDb();
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
    if (channelSettingsKeys.contains("logEnabled")) {
        settings.m_logEnabled = swgSettings->getLogEnabled() != 0;
    }
    if (channelSettingsKeys.contains("logFilename")) {
        settings.m_logFilename = *swgSettings->getLogFilename();
    }
    if (channelSettingsKeys.contains("registered")) {
        settings.m_registered = swgSettings->getRegistered() != 0;
    }
}

int VARADemod::webapiReportGet(
        SWGSDRangel::SWGChannelReport& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setVaraDemodReport(new SWGSDRangel::SWGVARADemodReport());
    response.getVaraDemodReport()->init();
    webapiFormatChannelReport(response);
    return 200;
}

void VARADemod::webapiFormatChannelSettings(SWGSDRangel::SWGChannelSettings& response, const VARADemodSettings& settings)
{
    SWGSDRangel::SWGVARADemodSettings *swgSettings = response.getVaraDemodSettings();

    swgSettings->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    swgSettings->setMode(settings.m_mode);
    if (swgSettings->getCallsign()) {
        *swgSettings->getCallsign() = settings.m_callsign;
    } else {
        swgSettings->setCallsign(new QString(settings.m_callsign));
    }
    if (swgSettings->getAddress()) {
        *swgSettings->getAddress() = settings.m_address;
    } else {
        swgSettings->setAddress(new QString(settings.m_address));
    }
    swgSettings->setCmdPort(settings.m_cmdPort);
    swgSettings->setKissEnabled(settings.m_kissEnabled ? 1 : 0);
    swgSettings->setKissPort(settings.m_kissPort);
    swgSettings->setDriveDb(settings.m_driveDb);
    swgSettings->setRxLagMs(settings.m_rxLagMs);
    swgSettings->setFmDeviation(settings.m_fmDeviation);
    swgSettings->setAgc(settings.m_agc ? 1 : 0);
    swgSettings->setRxGainDb(settings.m_rxGainDb);
    swgSettings->setVolume(settings.m_volume);
    swgSettings->setAudioMute(settings.m_audioMute ? 1 : 0);
    if (swgSettings->getAudioDeviceName()) {
        *swgSettings->getAudioDeviceName() = settings.m_audioDeviceName;
    } else {
        swgSettings->setAudioDeviceName(new QString(settings.m_audioDeviceName));
    }
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
    swgSettings->setLogEnabled(settings.m_logEnabled ? 1 : 0);
    swgSettings->setRegistered(settings.m_registered ? 1 : 0);
    if (swgSettings->getLogFilename()) {
        *swgSettings->getLogFilename() = settings.m_logFilename;
    } else {
        swgSettings->setLogFilename(new QString(settings.m_logFilename));
    }
}

void VARADemod::webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response)
{
    SWGSDRangel::SWGVARADemodReport *swgReport = response.getVaraDemodReport();
    double magsqAvg, magsqPeak;
    int nbMagsqSamples;
    getMagSqLevels(magsqAvg, magsqPeak, nbMagsqSamples);
    VARAEngine::Status status = m_engine->getStatus();

    swgReport->setChannelPowerDb(CalcDb::dbPower(magsqAvg));
    swgReport->setChannelSampleRate(m_running ? m_basebandSink->getChannelSampleRate() : 0);
    swgReport->setRunning(status.m_running ? 1 : 0);
    *swgReport->getError() = status.m_error;
    *swgReport->getState() = status.m_state;
    *swgReport->getPeer() = status.m_peer;
    *swgReport->getBandwidth() = status.m_bandwidth;
    swgReport->setLevel(status.m_level);
    swgReport->setAttempts(status.m_attempts);
    swgReport->setAfcHz(std::isnan(status.m_afcHz) ? 0.0f : status.m_afcHz);
    swgReport->setKeyed(status.m_keyed ? 1 : 0);
    swgReport->setBusy(status.m_busy ? 1 : 0);
    swgReport->setTxLinked(status.m_txLinked ? 1 : 0);
    swgReport->setCmdPort(status.m_cmdPort);
    swgReport->setDataPort(status.m_dataPort);
    swgReport->setKissPort(status.m_kissPort);
    swgReport->setP2p(status.m_p2p ? 1 : 0);
}

void VARADemod::webapiReverseSendSettings(const QList<QString>& channelSettingsKeys, const VARADemodSettings& settings, bool force)
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

void VARADemod::sendChannelSettings(
    const QList<ObjectPipe*>& pipes,
    const QList<QString>& channelSettingsKeys,
    const VARADemodSettings& settings,
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

void VARADemod::webapiFormatChannelSettings(
        const QList<QString>& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings *swgChannelSettings,
        const VARADemodSettings& settings,
        bool force
)
{
    swgChannelSettings->setDirection(0); // Single sink (Rx)
    swgChannelSettings->setOriginatorChannelIndex(getIndexInDeviceSet());
    swgChannelSettings->setOriginatorDeviceSetIndex(getDeviceSetIndex());
    swgChannelSettings->setChannelType(new QString(m_channelId));
    swgChannelSettings->setVaraDemodSettings(new SWGSDRangel::SWGVARADemodSettings());
    SWGSDRangel::SWGVARADemodSettings *swgSettings = swgChannelSettings->getVaraDemodSettings();

    // transfer data that has been modified. When force is on transfer all data except reverse API data

    if (channelSettingsKeys.contains("inputFrequencyOffset") || force) {
        swgSettings->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    }
    if (channelSettingsKeys.contains("mode") || force) {
        swgSettings->setMode(settings.m_mode);
    }
    if (channelSettingsKeys.contains("callsign") || force) {
        swgSettings->setCallsign(new QString(settings.m_callsign));
    }
    if (channelSettingsKeys.contains("address") || force) {
        swgSettings->setAddress(new QString(settings.m_address));
    }
    if (channelSettingsKeys.contains("cmdPort") || force) {
        swgSettings->setCmdPort(settings.m_cmdPort);
    }
    if (channelSettingsKeys.contains("kissEnabled") || force) {
        swgSettings->setKissEnabled(settings.m_kissEnabled ? 1 : 0);
    }
    if (channelSettingsKeys.contains("kissPort") || force) {
        swgSettings->setKissPort(settings.m_kissPort);
    }
    if (channelSettingsKeys.contains("driveDb") || force) {
        swgSettings->setDriveDb(settings.m_driveDb);
    }
    if (channelSettingsKeys.contains("rxLagMs") || force) {
        swgSettings->setRxLagMs(settings.m_rxLagMs);
    }
    if (channelSettingsKeys.contains("fmDeviation") || force) {
        swgSettings->setFmDeviation(settings.m_fmDeviation);
    }
    if (channelSettingsKeys.contains("agc") || force) {
        swgSettings->setAgc(settings.m_agc ? 1 : 0);
    }
    if (channelSettingsKeys.contains("rxGainDb") || force) {
        swgSettings->setRxGainDb(settings.m_rxGainDb);
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
    if (channelSettingsKeys.contains("logEnabled") || force) {
        swgSettings->setLogEnabled(settings.m_logEnabled ? 1 : 0);
    }
    if (channelSettingsKeys.contains("logFilename") || force) {
        swgSettings->setLogFilename(new QString(settings.m_logFilename));
    }
    if (channelSettingsKeys.contains("registered") || force) {
        swgSettings->setRegistered(settings.m_registered ? 1 : 0);
    }
}

void VARADemod::networkManagerFinished(QNetworkReply *reply)
{
    QNetworkReply::NetworkError replyError = reply->error();

    if (replyError)
    {
        qWarning() << "VARADemod::networkManagerFinished:"
                << " error(" << (int) replyError
                << "): " << replyError
                << ": " << reply->errorString();
    }
    else
    {
        QString answer = reply->readAll();
        answer.chop(1); // remove last \n
        qDebug("VARADemod::networkManagerFinished: reply:\n%s", answer.toStdString().c_str());
    }

    reply->deleteLater();
}

void VARADemod::handleIndexInDeviceSetChanged(int index)
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

int VARADemod::getChannelSampleRate() const
{
    return m_basebandSink ? m_basebandSink->getChannelSampleRate() : 0;
}
