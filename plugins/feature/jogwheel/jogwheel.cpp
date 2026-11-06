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
#include <QNetworkDatagram>
#include <QNetworkReply>
#include <QBuffer>
#include <QRegularExpression>
#include <QUdpSocket>

#include "SWGFeatureSettings.h"
#include "SWGFeatureReport.h"
#include "SWGFeatureActions.h"
#include "SWGJogWheelSettings.h"
#include "SWGJogWheelReport.h"
#include "SWGJogWheelActions.h"

#include "channel/channelapi.h"
#include "channel/channelwebapiutils.h"
#include "device/deviceapi.h"
#include "device/deviceset.h"
#include "dsp/devicesamplemimo.h"
#include "maincore.h"
#include "settings/serializable.h"

#include "jogwheel.h"

MESSAGE_CLASS_DEFINITION(JogWheel::MsgConfigureJogWheel, Message)
MESSAGE_CLASS_DEFINITION(JogWheel::MsgJog, Message)
MESSAGE_CLASS_DEFINITION(JogWheel::MsgResetTotal, Message)
MESSAGE_CLASS_DEFINITION(JogWheel::MsgReportTotal, Message)
MESSAGE_CLASS_DEFINITION(JogWheel::MsgReportUDP, Message)

const char* const JogWheel::m_featureIdURI = "sdrangel.feature.jogwheel";
const char* const JogWheel::m_featureId = "JogWheel";

JogWheel::JogWheel(WebAPIAdapterInterface *webAPIAdapterInterface) :
    Feature(m_featureIdURI, webAPIAdapterInterface),
    m_udpSocket(nullptr),
    m_pendingHz(0),
    m_totalHz(0),
    m_lastTickMs(0),
    m_tickRate(0.0),
    m_lastDirection(0),
    m_multiplier(1)
{
    qDebug("JogWheel::JogWheel: webAPIAdapterInterface: %p", webAPIAdapterInterface);
    setObjectName(m_featureId);
    setState(StIdle);
    m_errorMessage = "JogWheel error";
    m_networkManager = new QNetworkAccessManager();
    QObject::connect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &JogWheel::networkManagerFinished
    );
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(m_flushIntervalMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &JogWheel::flush);
    m_clock.start();
}

JogWheel::~JogWheel()
{
    closeUDP();
    QObject::disconnect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &JogWheel::networkManagerFinished
    );
    delete m_networkManager;
}

bool JogWheel::handleMessage(const Message& cmd)
{
    if (MsgConfigureJogWheel::match(cmd))
    {
        const MsgConfigureJogWheel& cfg = (const MsgConfigureJogWheel&) cmd;
        qDebug() << "JogWheel::handleMessage: MsgConfigureJogWheel";
        applySettings(cfg.getSettings(), cfg.getSettingsKeys(), cfg.getForce());
        return true;
    }
    else if (MsgJog::match(cmd))
    {
        const MsgJog& msg = (const MsgJog&) cmd;

        if (msg.getTicks() != 0) {
            jog(msg.getTicks());
        }
        if (msg.getDeltaHz() != 0) {
            moveHz(msg.getDeltaHz());
        }

        return true;
    }
    else if (MsgResetTotal::match(cmd))
    {
        resetTotal();
        return true;
    }

    return false;
}

QByteArray JogWheel::serialize() const
{
    return m_settings.serialize();
}

bool JogWheel::deserialize(const QByteArray& data)
{
    if (m_settings.deserialize(data))
    {
        MsgConfigureJogWheel *msg = MsgConfigureJogWheel::create(m_settings, QStringList(), true);
        m_inputMessageQueue.push(msg);
        return true;
    }
    else
    {
        m_settings.resetToDefaults();
        MsgConfigureJogWheel *msg = MsgConfigureJogWheel::create(m_settings, QStringList(), true);
        m_inputMessageQueue.push(msg);
        return false;
    }
}

void JogWheel::applySettings(const JogWheelSettings& settings, const QStringList& settingsKeys, bool force)
{
    qDebug() << "JogWheel::applySettings:" << settings.getDebugString(settingsKeys, force) << " force: " << force;

    bool targetChanged = (settingsKeys.contains("target") && (settings.m_target != m_settings.m_target))
        || (settingsKeys.contains("linkedTarget") && (settings.m_linkedTarget != m_settings.m_linkedTarget))
        || force;
    bool udpChanged = settingsKeys.contains("udpEnabled")
        || settingsKeys.contains("udpAddress")
        || settingsKeys.contains("udpPort")
        || force;

    if (settings.m_useReverseAPI)
    {
        bool fullUpdate = (settingsKeys.contains("useReverseAPI") && settings.m_useReverseAPI) ||
                settingsKeys.contains("reverseAPIAddress") ||
                settingsKeys.contains("reverseAPIPort") ||
                settingsKeys.contains("reverseAPIFeatureSetIndex") ||
                settingsKeys.contains("reverseAPIFeatureIndex");
        webapiReverseSendSettings(settingsKeys, settings, fullUpdate || force);
    }

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    if (m_settings.m_stepHz < 1) {
        m_settings.m_stepHz = 1;
    }

    // Turns not yet applied were meant for the old target. Count afresh for the new one
    if (targetChanged)
    {
        m_pendingHz = 0;
        resetTotal();
    }

    if (udpChanged)
    {
        closeUDP();

        if (m_settings.m_udpEnabled) {
            openUDP();
        } else if (getMessageQueueToGUI()) {
            getMessageQueueToGUI()->push(MsgReportUDP::create(false, ""));
        }
    }
}

void JogWheel::openUDP()
{
    m_udpSocket = new QUdpSocket(this);
    QHostAddress address(m_settings.m_udpAddress);

    if (address.isNull()) {
        address = QHostAddress::Any;
    }

    QString error;

    if (m_udpSocket->bind(address, m_settings.m_udpPort))
    {
        qDebug() << "JogWheel::openUDP: listening on" << address.toString() << m_settings.m_udpPort;
        connect(m_udpSocket, &QUdpSocket::readyRead, this, &JogWheel::readUDP);
    }
    else
    {
        error = m_udpSocket->errorString();
        qWarning() << "JogWheel::openUDP: failed to bind" << address.toString() << m_settings.m_udpPort << error;
        delete m_udpSocket;
        m_udpSocket = nullptr;
    }

    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(MsgReportUDP::create(m_udpSocket != nullptr, error));
    }
}

void JogWheel::closeUDP()
{
    if (m_udpSocket)
    {
        m_udpSocket->close();
        delete m_udpSocket;
        m_udpSocket = nullptr;
    }
}

void JogWheel::readUDP()
{
    while (m_udpSocket && m_udpSocket->hasPendingDatagrams())
    {
        QNetworkDatagram datagram = m_udpSocket->receiveDatagram();
        const QStringList commands = QString::fromLatin1(datagram.data()).split(QRegularExpression("[\\r\\n;]"), Qt::SkipEmptyParts);

        for (const auto& command : commands) {
            processCommand(command.trimmed());
        }
    }
}

// Text commands, one per line or separated by ';':
//   J<n> or <n>  Turn by n detents, E.g. "J-3", "+1", "2". Step size and acceleration apply
//   H<n>         Move by n Hz
//   S<n>         Set the step size to n Hz
//   S            Next step size in the list (E.g. for an encoder's push button)
//   R            Reset the total change
void JogWheel::processCommand(const QString& command)
{
    if (command.isEmpty()) {
        return;
    }

    QChar c = command[0].toUpper();
    QString arg = command.mid(1).trimmed();
    bool ok = false;

    if (c.isDigit() || (c == '+') || (c == '-'))
    {
        int ticks = command.toInt(&ok);

        if (ok) {
            jog(ticks);
        }
    }
    else if (c == 'J')
    {
        int ticks = arg.toInt(&ok);

        if (ok) {
            jog(ticks);
        }
    }
    else if (c == 'H')
    {
        qint64 deltaHz = arg.toLongLong(&ok);

        if (ok) {
            moveHz(deltaHz);
        }
    }
    else if (c == 'S')
    {
        if (arg.isEmpty())
        {
            cycleStep();
            ok = true;
        }
        else
        {
            int step = arg.toInt(&ok);

            if (ok && (step >= 1))
            {
                JogWheelSettings settings = m_settings;
                settings.m_stepHz = step;
                applySettings(settings, {"stepHz"});

                if (getMessageQueueToGUI()) {
                    getMessageQueueToGUI()->push(MsgConfigureJogWheel::create(settings, {"stepHz"}, false));
                }
            }
        }
    }
    else if (c == 'R')
    {
        resetTotal();
        ok = true;
    }

    if (!ok) {
        qDebug() << "JogWheel::processCommand: ignoring" << command;
    }
}

void JogWheel::cycleStep()
{
    const QVector<int>& steps = JogWheelSettings::m_steps;
    int next = steps[0];

    for (int step : steps)
    {
        if (step > m_settings.m_stepHz)
        {
            next = step;
            break;
        }
    }

    JogWheelSettings settings = m_settings;
    settings.m_stepHz = next;
    applySettings(settings, {"stepHz"});

    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(MsgConfigureJogWheel::create(settings, {"stepHz"}, false));
    }
}

// Like a rig's tuning knob, turning quickly multiplies the step. The rate is smoothed over a few
// readings, and resets on a pause or when the direction changes, so single detents are always exact
void JogWheel::jog(int ticks)
{
    if (ticks == 0) {
        return;
    }

    int direction = ticks > 0 ? 1 : -1;
    qint64 now = m_clock.elapsed();
    qint64 dt = now - m_lastTickMs;
    m_lastTickMs = now;

    if ((direction != m_lastDirection) || (dt > 250))
    {
        m_tickRate = 0.0;
    }
    else
    {
        double rate = std::abs(ticks) * 1000.0 / std::max<qint64>(dt, 5);
        m_tickRate = 0.6 * m_tickRate + 0.4 * rate;
    }

    m_lastDirection = direction;
    m_multiplier = 1;

    if (m_settings.m_acceleration)
    {
        if (m_tickRate >= 80.0) {
            m_multiplier = 10;
        } else if (m_tickRate >= 40.0) {
            m_multiplier = 5;
        } else if (m_tickRate >= 20.0) {
            m_multiplier = 2;
        }
    }

    moveHz((qint64) ticks * m_settings.m_stepHz * m_multiplier);
}

void JogWheel::moveHz(qint64 deltaHz)
{
    m_pendingHz += deltaHz;

    // Apply the first change straight away. Later ones are gathered until the timer expires
    if (!m_flushTimer.isActive()) {
        flush();
    }
}

void JogWheel::flush()
{
    if (m_pendingHz == 0) {
        return;
    }

    qint64 deltaHz = m_pendingHz;
    m_pendingHz = 0;

    if (moveTarget(m_settings.m_target, deltaHz))
    {
        if (!m_settings.m_linkedTarget.isEmpty()) {
            moveTarget(m_settings.m_linkedTarget, m_settings.m_linkInverted ? -deltaHz : deltaHz);
        }

        m_totalHz += deltaHz;
        reportTotal();
    }

    m_flushTimer.start();
}

bool JogWheel::moveTarget(const QString& target, qint64 deltaHz)
{
    unsigned int deviceSetIndex, channelIndex;

    if (target.isEmpty()) {
        return false;
    }

    if (MainCore::getDeviceAndChannelIndexFromId(target, deviceSetIndex, channelIndex))
    {
        int offset;

        if (!ChannelWebAPIUtils::getFrequencyOffset(deviceSetIndex, channelIndex, offset))
        {
            qDebug() << "JogWheel::moveTarget: no channel" << target;
            return false;
        }

        return ChannelWebAPIUtils::setFrequencyOffset(deviceSetIndex, channelIndex, offset + (int) deltaHz);
    }
    else if (MainCore::getDeviceSetIndexFromId(target, deviceSetIndex))
    {
        double centerFrequency;

        if (!ChannelWebAPIUtils::getCenterFrequency(deviceSetIndex, centerFrequency))
        {
            qDebug() << "JogWheel::moveTarget: no device set" << target;
            return false;
        }

        return ChannelWebAPIUtils::setCenterFrequency(deviceSetIndex, centerFrequency + deltaHz);
    }

    return false;
}

// Centre frequency of the device stream a channel is on. A MIMO device has one for each stream and
// direction, which ChannelWebAPIUtils::getCenterFrequency doesn't support.
bool JogWheel::getChannelCenterFrequency(unsigned int deviceSetIndex, unsigned int channelIndex, double& centerFrequency)
{
    std::vector<DeviceSet*> deviceSets = MainCore::instance()->getDeviceSets();

    if (deviceSetIndex >= deviceSets.size()) {
        return false;
    }

    DeviceSet *deviceSet = deviceSets[deviceSetIndex];

    if (!deviceSet->m_deviceMIMOEngine) {
        return ChannelWebAPIUtils::getCenterFrequency(deviceSetIndex, centerFrequency);
    }

    ChannelAPI *channel = deviceSet->getChannelAt(channelIndex);
    DeviceSampleMIMO *mimo = deviceSet->m_deviceAPI->getSampleMIMO();

    if (!channel || !mimo) {
        return false;
    }

    int streamIndex = std::max(channel->getStreamIndex(), 0);

    switch (channel->getStreamType())
    {
    case ChannelAPI::StreamSingleSink:
        centerFrequency = mimo->getSourceCenterFrequency(streamIndex);
        return true;
    case ChannelAPI::StreamSingleSource:
        centerFrequency = mimo->getSinkCenterFrequency(streamIndex);
        return true;
    default:
        return false;
    }
}

bool JogWheel::getTargetFrequency(const QString& target, qint64& frequency)
{
    unsigned int deviceSetIndex, channelIndex;
    double centerFrequency;

    if (target.isEmpty()) {
        return false;
    }

    if (MainCore::getDeviceAndChannelIndexFromId(target, deviceSetIndex, channelIndex))
    {
        int offset;

        if (getChannelCenterFrequency(deviceSetIndex, channelIndex, centerFrequency)
            && ChannelWebAPIUtils::getFrequencyOffset(deviceSetIndex, channelIndex, offset))
        {
            frequency = std::llround(centerFrequency) + offset;
            return true;
        }
    }
    else if (MainCore::getDeviceSetIndexFromId(target, deviceSetIndex))
    {
        if (ChannelWebAPIUtils::getCenterFrequency(deviceSetIndex, centerFrequency))
        {
            frequency = std::llround(centerFrequency);
            return true;
        }
    }

    return false;
}

void JogWheel::resetTotal()
{
    m_totalHz = 0;
    reportTotal();
}

void JogWheel::reportTotal()
{
    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(MsgReportTotal::create(m_totalHz, m_multiplier));
    }
}

int JogWheel::webapiSettingsGet(
    SWGSDRangel::SWGFeatureSettings& response,
    QString& errorMessage)
{
    (void) errorMessage;
    response.setJogWheelSettings(new SWGSDRangel::SWGJogWheelSettings());
    response.getJogWheelSettings()->init();
    webapiFormatFeatureSettings(response, m_settings);
    return 200;
}

int JogWheel::webapiSettingsPutPatch(
    bool force,
    const QStringList& featureSettingsKeys,
    SWGSDRangel::SWGFeatureSettings& response,
    QString& errorMessage)
{
    (void) errorMessage;
    JogWheelSettings settings = m_settings;
    webapiUpdateFeatureSettings(settings, featureSettingsKeys, response);

    MsgConfigureJogWheel *msg = MsgConfigureJogWheel::create(settings, featureSettingsKeys, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue)
    {
        MsgConfigureJogWheel *msgToGUI = MsgConfigureJogWheel::create(settings, featureSettingsKeys, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatFeatureSettings(response, settings);
    return 200;
}

int JogWheel::webapiReportGet(
    SWGSDRangel::SWGFeatureReport& response,
    QString& errorMessage)
{
    (void) errorMessage;
    response.setJogWheelReport(new SWGSDRangel::SWGJogWheelReport());
    response.getJogWheelReport()->init();

    qint64 frequency = 0;
    getTargetFrequency(m_settings.m_target, frequency);
    response.getJogWheelReport()->setTargetFrequency(frequency);
    frequency = 0;
    getTargetFrequency(m_settings.m_linkedTarget, frequency);
    response.getJogWheelReport()->setLinkedFrequency(frequency);
    response.getJogWheelReport()->setTotalHz(m_totalHz);

    return 200;
}

int JogWheel::webapiActionsPost(
    const QStringList& featureActionsKeys,
    SWGSDRangel::SWGFeatureActions& query,
    QString& errorMessage)
{
    SWGSDRangel::SWGJogWheelActions *swgJogWheelActions = query.getJogWheelActions();

    if (!swgJogWheelActions)
    {
        errorMessage = "Missing JogWheelActions in query";
        return 400;
    }

    bool unknownAction = true;

    if (featureActionsKeys.contains("jog"))
    {
        m_inputMessageQueue.push(MsgJog::create(swgJogWheelActions->getJog()));
        unknownAction = false;
    }
    if (featureActionsKeys.contains("deltaHz"))
    {
        m_inputMessageQueue.push(MsgJog::createHz(swgJogWheelActions->getDeltaHz()));
        unknownAction = false;
    }
    if (featureActionsKeys.contains("resetTotal"))
    {
        if (swgJogWheelActions->getResetTotal() != 0) {
            m_inputMessageQueue.push(MsgResetTotal::create());
        }
        unknownAction = false;
    }

    if (unknownAction)
    {
        errorMessage = "Unknown action";
        return 400;
    }

    return 202;
}

void JogWheel::webapiFormatFeatureSettings(
    SWGSDRangel::SWGFeatureSettings& response,
    const JogWheelSettings& settings)
{
    SWGSDRangel::SWGJogWheelSettings *swg = response.getJogWheelSettings();

    if (swg->getTitle()) {
        *swg->getTitle() = settings.m_title;
    } else {
        swg->setTitle(new QString(settings.m_title));
    }

    swg->setRgbColor(settings.m_rgbColor);

    if (swg->getTarget()) {
        *swg->getTarget() = settings.m_target;
    } else {
        swg->setTarget(new QString(settings.m_target));
    }

    if (swg->getLinkedTarget()) {
        *swg->getLinkedTarget() = settings.m_linkedTarget;
    } else {
        swg->setLinkedTarget(new QString(settings.m_linkedTarget));
    }

    swg->setLinkInverted(settings.m_linkInverted ? 1 : 0);
    swg->setStepHz(settings.m_stepHz);
    swg->setAcceleration(settings.m_acceleration ? 1 : 0);
    swg->setUdpEnabled(settings.m_udpEnabled ? 1 : 0);

    if (swg->getUdpAddress()) {
        *swg->getUdpAddress() = settings.m_udpAddress;
    } else {
        swg->setUdpAddress(new QString(settings.m_udpAddress));
    }

    swg->setUdpPort(settings.m_udpPort);
    swg->setUseReverseApi(settings.m_useReverseAPI ? 1 : 0);

    if (swg->getReverseApiAddress()) {
        *swg->getReverseApiAddress() = settings.m_reverseAPIAddress;
    } else {
        swg->setReverseApiAddress(new QString(settings.m_reverseAPIAddress));
    }

    swg->setReverseApiPort(settings.m_reverseAPIPort);
    swg->setReverseApiFeatureSetIndex(settings.m_reverseAPIFeatureSetIndex);
    swg->setReverseApiFeatureIndex(settings.m_reverseAPIFeatureIndex);

    if (settings.m_rollupState)
    {
        if (swg->getRollupState())
        {
            settings.m_rollupState->formatTo(swg->getRollupState());
        }
        else
        {
            SWGSDRangel::SWGRollupState *swgRollupState = new SWGSDRangel::SWGRollupState();
            settings.m_rollupState->formatTo(swgRollupState);
            swg->setRollupState(swgRollupState);
        }
    }
}

void JogWheel::webapiUpdateFeatureSettings(
    JogWheelSettings& settings,
    const QStringList& featureSettingsKeys,
    SWGSDRangel::SWGFeatureSettings& response)
{
    SWGSDRangel::SWGJogWheelSettings *swg = response.getJogWheelSettings();

    if (featureSettingsKeys.contains("title")) {
        settings.m_title = *swg->getTitle();
    }
    if (featureSettingsKeys.contains("rgbColor")) {
        settings.m_rgbColor = swg->getRgbColor();
    }
    if (featureSettingsKeys.contains("target")) {
        settings.m_target = *swg->getTarget();
    }
    if (featureSettingsKeys.contains("linkedTarget")) {
        settings.m_linkedTarget = *swg->getLinkedTarget();
    }
    if (featureSettingsKeys.contains("linkInverted")) {
        settings.m_linkInverted = swg->getLinkInverted() != 0;
    }
    if (featureSettingsKeys.contains("stepHz")) {
        settings.m_stepHz = std::max(1, swg->getStepHz());
    }
    if (featureSettingsKeys.contains("acceleration")) {
        settings.m_acceleration = swg->getAcceleration() != 0;
    }
    if (featureSettingsKeys.contains("udpEnabled")) {
        settings.m_udpEnabled = swg->getUdpEnabled() != 0;
    }
    if (featureSettingsKeys.contains("udpAddress")) {
        settings.m_udpAddress = *swg->getUdpAddress();
    }
    if (featureSettingsKeys.contains("udpPort")) {
        settings.m_udpPort = swg->getUdpPort();
    }
    if (featureSettingsKeys.contains("useReverseAPI")) {
        settings.m_useReverseAPI = swg->getUseReverseApi() != 0;
    }
    if (featureSettingsKeys.contains("reverseAPIAddress")) {
        settings.m_reverseAPIAddress = *swg->getReverseApiAddress();
    }
    if (featureSettingsKeys.contains("reverseAPIPort")) {
        settings.m_reverseAPIPort = swg->getReverseApiPort();
    }
    if (featureSettingsKeys.contains("reverseAPIFeatureSetIndex")) {
        settings.m_reverseAPIFeatureSetIndex = swg->getReverseApiFeatureSetIndex();
    }
    if (featureSettingsKeys.contains("reverseAPIFeatureIndex")) {
        settings.m_reverseAPIFeatureIndex = swg->getReverseApiFeatureIndex();
    }
    if (settings.m_rollupState && featureSettingsKeys.contains("rollupState")) {
        settings.m_rollupState->updateFrom(featureSettingsKeys, swg->getRollupState());
    }
}

void JogWheel::webapiReverseSendSettings(const QStringList& featureSettingsKeys, const JogWheelSettings& settings, bool force)
{
    SWGSDRangel::SWGFeatureSettings *swgFeatureSettings = new SWGSDRangel::SWGFeatureSettings();
    swgFeatureSettings->setFeatureType(new QString("JogWheel"));
    swgFeatureSettings->setJogWheelSettings(new SWGSDRangel::SWGJogWheelSettings());
    SWGSDRangel::SWGJogWheelSettings *swg = swgFeatureSettings->getJogWheelSettings();

    // transfer data that has been modified. When force is on transfer all data except reverse API data

    if (featureSettingsKeys.contains("title") || force) {
        swg->setTitle(new QString(settings.m_title));
    }
    if (featureSettingsKeys.contains("rgbColor") || force) {
        swg->setRgbColor(settings.m_rgbColor);
    }
    if (featureSettingsKeys.contains("target") || force) {
        swg->setTarget(new QString(settings.m_target));
    }
    if (featureSettingsKeys.contains("linkedTarget") || force) {
        swg->setLinkedTarget(new QString(settings.m_linkedTarget));
    }
    if (featureSettingsKeys.contains("linkInverted") || force) {
        swg->setLinkInverted(settings.m_linkInverted ? 1 : 0);
    }
    if (featureSettingsKeys.contains("stepHz") || force) {
        swg->setStepHz(settings.m_stepHz);
    }
    if (featureSettingsKeys.contains("acceleration") || force) {
        swg->setAcceleration(settings.m_acceleration ? 1 : 0);
    }
    if (featureSettingsKeys.contains("udpEnabled") || force) {
        swg->setUdpEnabled(settings.m_udpEnabled ? 1 : 0);
    }
    if (featureSettingsKeys.contains("udpAddress") || force) {
        swg->setUdpAddress(new QString(settings.m_udpAddress));
    }
    if (featureSettingsKeys.contains("udpPort") || force) {
        swg->setUdpPort(settings.m_udpPort);
    }

    QString featureSettingsURL = QString("http://%1:%2/sdrangel/featureset/%3/feature/%4/settings")
            .arg(settings.m_reverseAPIAddress)
            .arg(settings.m_reverseAPIPort)
            .arg(settings.m_reverseAPIFeatureSetIndex)
            .arg(settings.m_reverseAPIFeatureIndex);
    m_networkRequest.setUrl(QUrl(featureSettingsURL));
    m_networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QBuffer *buffer = new QBuffer();
    buffer->open((QBuffer::ReadWrite));
    buffer->write(swgFeatureSettings->asJson().toUtf8());
    buffer->seek(0);

    // Always use PATCH to avoid passing reverse API settings
    QNetworkReply *reply = m_networkManager->sendCustomRequest(m_networkRequest, "PATCH", buffer);
    buffer->setParent(reply);

    delete swgFeatureSettings;
}

void JogWheel::networkManagerFinished(QNetworkReply *reply)
{
    QNetworkReply::NetworkError replyError = reply->error();

    if (replyError)
    {
        qWarning() << "JogWheel::networkManagerFinished:"
                << " error(" << (int) replyError
                << "): " << replyError
                << ": " << reply->errorString();
    }
    else
    {
        QString answer = reply->readAll();
        answer.chop(1); // remove last \n
        qDebug("JogWheel::networkManagerFinished: reply:\n%s", answer.toStdString().c_str());
    }

    reply->deleteLater();
}
