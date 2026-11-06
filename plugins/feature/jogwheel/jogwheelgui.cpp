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

#include <QComboBox>
#include <QFontDatabase>

#include "gui/basicfeaturesettingsdialog.h"
#include "gui/dialogpositioner.h"
#include "feature/featureuiset.h"
#include "maincore.h"

#include "ui_jogwheelgui.h"
#include "jogwheel.h"
#include "jogwheelgui.h"

JogWheelGUI* JogWheelGUI::create(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature)
{
    return new JogWheelGUI(pluginAPI, featureUISet, feature);
}

void JogWheelGUI::destroy()
{
    delete this;
}

void JogWheelGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applyAllSettings();
}

QByteArray JogWheelGUI::serialize() const
{
    return m_settings.serialize();
}

bool JogWheelGUI::deserialize(const QByteArray& data)
{
    if (m_settings.deserialize(data))
    {
        m_feature->setWorkspaceIndex(m_settings.m_workspaceIndex);
        displaySettings();
        applyAllSettings();
        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

bool JogWheelGUI::handleMessage(const Message& message)
{
    if (JogWheel::MsgConfigureJogWheel::match(message))
    {
        qDebug("JogWheelGUI::handleMessage: JogWheel::MsgConfigureJogWheel");
        const JogWheel::MsgConfigureJogWheel& cfg = (const JogWheel::MsgConfigureJogWheel&) message;

        if (cfg.getForce()) {
            m_settings = cfg.getSettings();
        } else {
            m_settings.applySettings(cfg.getSettingsKeys(), cfg.getSettings());
        }

        blockApplySettings(true);
        displaySettings();
        blockApplySettings(false);
        return true;
    }
    else if (JogWheel::MsgReportTotal::match(message))
    {
        const JogWheel::MsgReportTotal& report = (const JogWheel::MsgReportTotal&) message;
        ui->total->setText(formatDelta(report.getTotalHz()));

        if (report.getMultiplier() > 1)
        {
            ui->multiplier->setText(QString("x%1").arg(report.getMultiplier()));
            m_multiplierTimer.start();
        }

        pollFrequencies();
        return true;
    }
    else if (JogWheel::MsgReportUDP::match(message))
    {
        const JogWheel::MsgReportUDP& report = (const JogWheel::MsgReportUDP&) message;

        if (report.getError().isEmpty())
        {
            ui->udpEnabled->setStyleSheet("");
            ui->udpEnabled->setToolTip(tr("Listen for jog commands from a hardware encoder over UDP (see the help for the protocol)"));
        }
        else
        {
            ui->udpEnabled->setStyleSheet("QToolButton { background-color : rgb(160, 40, 40); }");
            ui->udpEnabled->setToolTip(tr("Cannot listen on UDP: %1").arg(report.getError()));
        }

        return true;
    }

    return false;
}

void JogWheelGUI::handleInputMessages()
{
    Message* message;

    while ((message = getInputMessageQueue()->pop()))
    {
        if (handleMessage(*message)) {
            delete message;
        }
    }
}

void JogWheelGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;

    getRollupContents()->saveState(m_rollupState);
    applySetting("rollupState");
}

JogWheelGUI::JogWheelGUI(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature, QWidget* parent) :
    FeatureGUI(parent),
    ui(new Ui::JogWheelGUI),
    m_jogWheel(reinterpret_cast<JogWheel*>(feature)),
    m_doApplySettings(true),
    m_availableChannelHandler(QStringList(), m_channelKinds)
{
    (void) pluginAPI;
    (void) featureUISet;

    m_feature = feature;
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/feature/jogwheel/readme.md";
    RollupContents *rollupContents = getRollupContents();
    ui->setupUi(rollupContents);
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    sizeToContents();

    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));

    m_jogWheel->setMessageQueueToGUI(&m_inputMessageQueue);
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));

    QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    fixed.setPointSize(18);
    fixed.setBold(true);
    ui->targetFrequency->setFont(fixed);
    fixed.setPointSize(12);
    fixed.setBold(false);
    ui->linkedFrequency->setFont(fixed);

    for (int step : JogWheelSettings::m_steps) {
        ui->step->addItem(step >= 1000 ? QString("%1 kHz").arg(step / 1000.0) : QString("%1 Hz").arg(step), step);
    }

    connect(
        &m_availableChannelHandler,
        &AvailableChannelOrFeatureHandler::channelsOrFeaturesChanged,
        this,
        &JogWheelGUI::channelsChanged
    );
    connect(MainCore::instance(), &MainCore::deviceSetAdded, this, &JogWheelGUI::deviceSetsChanged);
    connect(MainCore::instance(), &MainCore::deviceSetRemoved, this, &JogWheelGUI::deviceSetsChanged);
    m_availableChannelHandler.scanAvailableChannelsAndFeatures();

    m_multiplierTimer.setSingleShot(true);
    m_multiplierTimer.setInterval(600);
    connect(&m_multiplierTimer, &QTimer::timeout, this, [this]() { ui->multiplier->setText(""); });
    connect(&m_pollTimer, &QTimer::timeout, this, &JogWheelGUI::pollFrequencies);
    m_pollTimer.start(m_pollIntervalMs);

    m_settings.setRollupState(&m_rollupState);

    displaySettings();
    makeUIConnections();
    applyAllSettings();
    m_resizer.enableChildMouseTracking();
}

JogWheelGUI::~JogWheelGUI()
{
    delete ui;
}

void JogWheelGUI::setWorkspaceIndex(int index)
{
    m_settings.m_workspaceIndex = index;
    m_feature->setWorkspaceIndex(index);
}

void JogWheelGUI::blockApplySettings(bool block)
{
    m_doApplySettings = !block;
}

void JogWheelGUI::displaySettings()
{
    setTitleColor(m_settings.m_rgbColor);
    setWindowTitle(m_settings.m_title);
    setTitle(m_settings.m_title);
    blockApplySettings(true);

    updateTargetLists();
    ui->linkSense->setCurrentIndex(m_settings.m_linkInverted ? 1 : 0);
    displayStep();
    ui->acceleration->setChecked(m_settings.m_acceleration);
    ui->udpEnabled->setChecked(m_settings.m_udpEnabled);
    ui->udpAddress->setText(m_settings.m_udpAddress);
    ui->udpPort->setValue(m_settings.m_udpPort);

    getRollupContents()->restoreState(m_rollupState);
    blockApplySettings(false);
    pollFrequencies();
}

void JogWheelGUI::displayStep()
{
    ui->step->blockSignals(true);
    int index = ui->step->findData(m_settings.m_stepHz);

    if (index < 0)
    {
        // Set through the Web API or UDP to a size not in the list
        ui->step->addItem(QString("%1 Hz").arg(m_settings.m_stepHz), m_settings.m_stepHz);
        index = ui->step->count() - 1;
    }

    ui->step->setCurrentIndex(index);
    ui->step->blockSignals(false);
}

// Targets may be given as "R0:1" through the Web API, while the GUI uses "R0:1 SSBDemod", so compare indexes
bool JogWheelGUI::sameTarget(const QString& a, const QString& b)
{
    if (a == b) {
        return true;
    }
    if (a.isEmpty() || b.isEmpty() || (a[0] != b[0])) {
        return false;
    }

    unsigned int deviceSetA, channelA, deviceSetB, channelB;
    bool channelIdA = MainCore::getDeviceAndChannelIndexFromId(a, deviceSetA, channelA);
    bool channelIdB = MainCore::getDeviceAndChannelIndexFromId(b, deviceSetB, channelB);

    if (channelIdA || channelIdB) {
        return channelIdA && channelIdB && (deviceSetA == deviceSetB) && (channelA == channelB);
    }

    return MainCore::getDeviceSetIndexFromId(a, deviceSetA)
        && MainCore::getDeviceSetIndexFromId(b, deviceSetB)
        && (deviceSetA == deviceSetB);
}

void JogWheelGUI::fillTargets(QComboBox *combo, const QString& selected, bool withNone)
{
    combo->blockSignals(true);
    combo->clear();

    if (withNone) {
        combo->addItem(tr("None"), QString());
    }

    const AvailableChannelOrFeatureList channels = m_availableChannelHandler.getAvailableChannelOrFeatureList();

    for (const auto& channel : channels) {
        combo->addItem(channel.getLongId(), channel.getLongId());
    }

    const QStringList deviceSetIds = MainCore::instance()->getDeviceSetIds(true, true, false);

    for (const auto& deviceSetId : deviceSetIds) {
        combo->addItem(tr("%1 device centre").arg(deviceSetId), deviceSetId);
    }

    int index = -1;

    for (int i = 0; i < combo->count(); i++)
    {
        if (sameTarget(combo->itemData(i).toString(), selected) || (withNone && (i == 0) && selected.isEmpty()))
        {
            index = i;
            break;
        }
    }

    // Keep a target that isn't there yet (E.g. a configuration loading features before device sets)
    if ((index < 0) && !selected.isEmpty())
    {
        combo->addItem(tr("%1 (not found)").arg(selected), selected);
        index = combo->count() - 1;
    }

    combo->setCurrentIndex(index);
    combo->blockSignals(false);
}

void JogWheelGUI::updateTargetLists()
{
    fillTargets(ui->target, m_settings.m_target, false);
    fillTargets(ui->linkedTarget, m_settings.m_linkedTarget, true);
}

void JogWheelGUI::channelsChanged(const QStringList& renameFrom, const QStringList& renameTo, const QStringList& removed, const QStringList& added)
{
    (void) removed;
    (void) added;
    QStringList keys;

    // Channels are renamed when one before them in their device set is removed
    for (int i = 0; (i < renameFrom.size()) && (i < renameTo.size()); i++)
    {
        if (m_settings.m_target == renameFrom[i])
        {
            m_settings.m_target = renameTo[i];
            keys.append("target");
        }
        if (m_settings.m_linkedTarget == renameFrom[i])
        {
            m_settings.m_linkedTarget = renameTo[i];
            keys.append("linkedTarget");
        }
    }

    updateTargetLists();

    if (!keys.isEmpty()) {
        applySettings(keys);
    }
}

void JogWheelGUI::deviceSetsChanged()
{
    updateTargetLists();
}

void JogWheelGUI::onMenuDialogCalled(const QPoint &p)
{
    if (m_contextMenuType == ContextMenuType::ContextMenuChannelSettings)
    {
        BasicFeatureSettingsDialog dialog(this);
        dialog.setTitle(m_settings.m_title);
        dialog.setUseReverseAPI(m_settings.m_useReverseAPI);
        dialog.setReverseAPIAddress(m_settings.m_reverseAPIAddress);
        dialog.setReverseAPIPort(m_settings.m_reverseAPIPort);
        dialog.setReverseAPIFeatureSetIndex(m_settings.m_reverseAPIFeatureSetIndex);
        dialog.setReverseAPIFeatureIndex(m_settings.m_reverseAPIFeatureIndex);
        dialog.setDefaultTitle(m_displayedName);

        dialog.move(p);
        new DialogPositioner(&dialog, false);
        dialog.exec();

        m_settings.m_title = dialog.getTitle();
        m_settings.m_useReverseAPI = dialog.useReverseAPI();
        m_settings.m_reverseAPIAddress = dialog.getReverseAPIAddress();
        m_settings.m_reverseAPIPort = dialog.getReverseAPIPort();
        m_settings.m_reverseAPIFeatureSetIndex = dialog.getReverseAPIFeatureSetIndex();
        m_settings.m_reverseAPIFeatureIndex = dialog.getReverseAPIFeatureIndex();

        setTitle(m_settings.m_title);
        setTitleColor(m_settings.m_rgbColor);

        applySettings({"title", "rgbColor", "useReverseAPI", "reverseAPIAddress", "reverseAPIPort",
            "reverseAPIFeatureSetIndex", "reverseAPIFeatureIndex"});
    }

    resetContextMenuType();
}

void JogWheelGUI::applySetting(const QString& settingsKey)
{
    applySettings({settingsKey});
}

void JogWheelGUI::applySettings(const QStringList& settingsKeys, bool force)
{
    m_settingsKeys.append(settingsKeys);

    if (m_doApplySettings)
    {
        JogWheel::MsgConfigureJogWheel* message = JogWheel::MsgConfigureJogWheel::create(m_settings, m_settingsKeys, force);
        m_jogWheel->getInputMessageQueue()->push(message);
        m_settingsKeys.clear();
    }
}

void JogWheelGUI::applyAllSettings()
{
    applySettings(QStringList(), true);
}

void JogWheelGUI::on_target_currentIndexChanged(int index)
{
    m_settings.m_target = index >= 0 ? ui->target->itemData(index).toString() : QString();
    applySetting("target");
    pollFrequencies();
}

void JogWheelGUI::on_linkedTarget_currentIndexChanged(int index)
{
    m_settings.m_linkedTarget = index >= 0 ? ui->linkedTarget->itemData(index).toString() : QString();
    applySetting("linkedTarget");
    pollFrequencies();
}

void JogWheelGUI::on_linkSense_currentIndexChanged(int index)
{
    m_settings.m_linkInverted = index == 1;
    applySetting("linkInverted");
}

void JogWheelGUI::on_step_currentIndexChanged(int index)
{
    if (index >= 0)
    {
        m_settings.m_stepHz = ui->step->itemData(index).toInt();
        applySetting("stepHz");
    }
}

void JogWheelGUI::on_acceleration_toggled(bool checked)
{
    m_settings.m_acceleration = checked;
    applySetting("acceleration");
}

void JogWheelGUI::on_udpEnabled_toggled(bool checked)
{
    m_settings.m_udpEnabled = checked;
    applySetting("udpEnabled");
}

void JogWheelGUI::on_udpAddress_editingFinished()
{
    if (ui->udpAddress->text() != m_settings.m_udpAddress)
    {
        m_settings.m_udpAddress = ui->udpAddress->text();
        applySetting("udpAddress");
    }
}

void JogWheelGUI::on_udpPort_editingFinished()
{
    if (ui->udpPort->value() != m_settings.m_udpPort)
    {
        m_settings.m_udpPort = ui->udpPort->value();
        applySetting("udpPort");
    }
}

void JogWheelGUI::on_resetTotal_clicked()
{
    m_jogWheel->getInputMessageQueue()->push(JogWheel::MsgResetTotal::create());
}

void JogWheelGUI::knobTurned(int detents)
{
    m_jogWheel->getInputMessageQueue()->push(JogWheel::MsgJog::create(detents));
}

void JogWheelGUI::pollFrequencies()
{
    qint64 frequency;

    if (JogWheel::getTargetFrequency(m_settings.m_target, frequency)) {
        ui->targetFrequency->setText(formatFrequency(frequency));
    } else {
        ui->targetFrequency->setText("---");
    }

    if (m_settings.m_linkedTarget.isEmpty()) {
        ui->linkedFrequency->setText("");
    } else if (JogWheel::getTargetFrequency(m_settings.m_linkedTarget, frequency)) {
        ui->linkedFrequency->setText(tr("Link %1").arg(formatFrequency(frequency)));
    } else {
        ui->linkedFrequency->setText(tr("Link ---"));
    }
}

// E.g. 435.123 456 MHz, so the Hz the wheel is moving are easy to read
QString JogWheelGUI::formatFrequency(qint64 frequency)
{
    QString sign = frequency < 0 ? "-" : "";
    qint64 f = std::abs(frequency);
    return QString("%1%2.%3 %4 MHz")
        .arg(sign)
        .arg(f / 1000000)
        .arg((f / 1000) % 1000, 3, 10, QChar('0'))
        .arg(f % 1000, 3, 10, QChar('0'));
}

QString JogWheelGUI::formatDelta(qint64 deltaHz)
{
    QString sign = deltaHz < 0 ? "-" : deltaHz > 0 ? "+" : "";
    qint64 d = std::abs(deltaHz);

    if (d < 10000) {
        return QString("Δ %1%2 Hz").arg(sign).arg(d);
    } else {
        return QString("Δ %1%2 kHz").arg(sign).arg(d / 1000.0, 0, 'f', 3);
    }
}

void JogWheelGUI::makeUIConnections()
{
    connect(ui->target, qOverload<int>(&QComboBox::currentIndexChanged), this, &JogWheelGUI::on_target_currentIndexChanged);
    connect(ui->linkedTarget, qOverload<int>(&QComboBox::currentIndexChanged), this, &JogWheelGUI::on_linkedTarget_currentIndexChanged);
    connect(ui->linkSense, qOverload<int>(&QComboBox::currentIndexChanged), this, &JogWheelGUI::on_linkSense_currentIndexChanged);
    connect(ui->step, qOverload<int>(&QComboBox::currentIndexChanged), this, &JogWheelGUI::on_step_currentIndexChanged);
    connect(ui->acceleration, &ButtonSwitch::toggled, this, &JogWheelGUI::on_acceleration_toggled);
    connect(ui->udpEnabled, &ButtonSwitch::toggled, this, &JogWheelGUI::on_udpEnabled_toggled);
    connect(ui->udpAddress, &QLineEdit::editingFinished, this, &JogWheelGUI::on_udpAddress_editingFinished);
    connect(ui->udpPort, &QSpinBox::editingFinished, this, &JogWheelGUI::on_udpPort_editingFinished);
    connect(ui->resetTotal, &QToolButton::clicked, this, &JogWheelGUI::on_resetTotal_clicked);
    connect(ui->knob, &JogWheelKnob::turned, this, &JogWheelGUI::knobTurned);
}
