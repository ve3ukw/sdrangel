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

#include <cmath>

#include <QDebug>

#include "channel/channelwebapiutils.h"
#include "device/deviceuiset.h"
#include "dsp/dspcommands.h"
#include "gui/basicchannelsettingsdialog.h"
#include "gui/dialpopup.h"
#include "gui/dialogpositioner.h"
#include "plugin/pluginapi.h"
#include "util/db.h"
#include "maincore.h"

#include "ui_varamodgui.h"
#include "varaengine.h"
#include "varamod.h"
#include "varamodgui.h"

VARAModGUI* VARAModGUI::create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSource *txChannel)
{
    VARAModGUI* gui = new VARAModGUI(pluginAPI, deviceUISet, txChannel);
    return gui;
}

void VARAModGUI::destroy()
{
    delete this;
}

void VARAModGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applySettings(QStringList(), true);
}

QByteArray VARAModGUI::serialize() const
{
    return m_settings.serialize();
}

bool VARAModGUI::deserialize(const QByteArray& data)
{
    if (m_settings.deserialize(data))
    {
        displaySettings();
        applySettings(QStringList(), true);
        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

bool VARAModGUI::handleMessage(const Message& message)
{
    if (VARAMod::MsgConfigureVARAMod::match(message))
    {
        qDebug("VARAModGUI::handleMessage: VARAMod::MsgConfigureVARAMod");
        const VARAMod::MsgConfigureVARAMod& cfg = (const VARAMod::MsgConfigureVARAMod&) message;

        if (cfg.getForce()) {
            m_settings = cfg.getSettings();
        } else {
            m_settings.applySettings(cfg.getSettingsKeys(), cfg.getSettings());
        }

        m_doApplySettings = false;
        m_channelMarker.updateSettings(static_cast<const ChannelMarker*>(m_settings.m_channelMarker));
        displaySettings();
        m_doApplySettings = true;
        return true;
    }
    else if (DSPSignalNotification::match(message))
    {
        const DSPSignalNotification& notif = (const DSPSignalNotification&) message;
        m_deviceCenterFrequency = notif.getCenterFrequency();
        m_basebandSampleRate = notif.getSampleRate();
        ui->deltaFrequency->setValueRange(false, 7, -m_basebandSampleRate/2, m_basebandSampleRate/2);
        ui->deltaFrequencyLabel->setToolTip(tr("Range %1 %L2 Hz").arg(QChar(0xB1)).arg(m_basebandSampleRate/2));
        updateAbsoluteCenterFrequency();
        return true;
    }

    return false;
}

void VARAModGUI::handleInputMessages()
{
    Message* message;

    while ((message = getInputMessageQueue()->pop()))
    {
        if (!handleMessage(*message)) {
            qDebug("%s: unhandled message: %s", Q_FUNC_INFO, message->getIdentifier());
        }

        delete message;
    }
}

void VARAModGUI::channelMarkerChangedByCursor()
{
    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void VARAModGUI::channelMarkerHighlightedByCursor()
{
    setHighlighted(m_channelMarker.getHighlighted());
}

void VARAModGUI::on_deltaFrequency_changed(qint64 value)
{
    m_channelMarker.setCenterFrequency(value);
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    updateAbsoluteCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void VARAModGUI::on_channelMute_toggled(bool checked)
{
    m_settings.m_channelMute = checked;
    applySettings(QStringList("channelMute"));
}

void VARAModGUI::on_cmdPort_editingFinished()
{
    if (ui->cmdPort->value() != m_settings.m_cmdPort)
    {
        m_settings.m_cmdPort = ui->cmdPort->value();
        applySettings(QStringList("cmdPort"));
    }
}

void VARAModGUI::on_fmDeviation_valueChanged(int value)
{
    m_settings.m_fmDeviation = value;
    applySettings(QStringList("fmDeviation"));
}

void VARAModGUI::on_gain_valueChanged(int value)
{
    m_settings.m_gain = value;
    applySettings(QStringList("gain"));
}

void VARAModGUI::on_linkFrequencyOffset_toggled(bool checked)
{
    m_settings.m_linkFrequencyOffset = checked;
    applySettings(QStringList("linkFrequencyOffset"));
}

void VARAModGUI::on_pttEnabled_toggled(bool checked)
{
    m_settings.m_pttEnabled = checked;
    applySettings(QStringList("pttEnabled"));
}

void VARAModGUI::on_pttAddress_editingFinished()
{
    QString address = ui->pttAddress->text().trimmed();

    if (address != m_settings.m_pttAddress)
    {
        m_settings.m_pttAddress = address;
        applySettings(QStringList("pttAddress"));
    }
}

void VARAModGUI::on_pttPort_editingFinished()
{
    if (ui->pttPort->value() != m_settings.m_pttPort)
    {
        m_settings.m_pttPort = ui->pttPort->value();
        applySettings(QStringList("pttPort"));
    }
}

void VARAModGUI::on_pttQuery_clicked()
{
    m_varaMod->getInputMessageQueue()->push(VARAMod::MsgQueryPTT::create());
}

void VARAModGUI::on_pttLeadMs_valueChanged(int value)
{
    m_settings.m_pttLeadMs = value;
    applySettings(QStringList("pttLeadMs"));
}

void VARAModGUI::on_pttTailMs_valueChanged(int value)
{
    m_settings.m_pttTailMs = value;
    applySettings(QStringList("pttTailMs"));
}

void VARAModGUI::on_txPathMs_valueChanged(int value)
{
    m_settings.m_txPathMs = value;
    applySettings(QStringList("txPathMs"));
}

void VARAModGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;

    getRollupContents()->saveState(m_rollupState);
    applySettings(QStringList("rollupState"));
}

void VARAModGUI::onMenuDialogCalled(const QPoint &p)
{
    if (m_contextMenuType == ContextMenuType::ContextMenuChannelSettings)
    {
        BasicChannelSettingsDialog dialog(&m_channelMarker, this);
        dialog.setUseReverseAPI(m_settings.m_useReverseAPI);
        dialog.setReverseAPIAddress(m_settings.m_reverseAPIAddress);
        dialog.setReverseAPIPort(m_settings.m_reverseAPIPort);
        dialog.setReverseAPIDeviceIndex(m_settings.m_reverseAPIDeviceIndex);
        dialog.setReverseAPIChannelIndex(m_settings.m_reverseAPIChannelIndex);
        dialog.setDefaultTitle(m_displayedName);

        if (m_deviceUISet->m_deviceMIMOEngine)
        {
            dialog.setNumberOfStreams(m_varaMod->getNumberOfDeviceStreams());
            dialog.setStreamIndex(m_settings.m_streamIndex);
        }

        dialog.move(p);
        new DialogPositioner(&dialog, false);
        dialog.exec();

        m_settings.m_rgbColor = m_channelMarker.getColor().rgb();
        m_settings.m_title = m_channelMarker.getTitle();
        m_settings.m_useReverseAPI = dialog.useReverseAPI();
        m_settings.m_reverseAPIAddress = dialog.getReverseAPIAddress();
        m_settings.m_reverseAPIPort = dialog.getReverseAPIPort();
        m_settings.m_reverseAPIDeviceIndex = dialog.getReverseAPIDeviceIndex();
        m_settings.m_reverseAPIChannelIndex = dialog.getReverseAPIChannelIndex();

        setWindowTitle(m_settings.m_title);
        setTitle(m_channelMarker.getTitle());
        setTitleColor(m_settings.m_rgbColor);

        if (m_deviceUISet->m_deviceMIMOEngine)
        {
            m_settings.m_streamIndex = dialog.getSelectedStreamIndex();
            m_channelMarker.clearStreamIndexes();
            m_channelMarker.addStreamIndex(m_settings.m_streamIndex);
            updateIndexLabel();
        }

        applySettings(QStringList({
            "title",
            "rgbColor",
            "useReverseAPI",
            "reverseAPIAddress",
            "reverseAPIPort",
            "reverseAPIDeviceIndex",
            "reverseAPIChannelIndex",
            "streamIndex"
        }));
    }

    resetContextMenuType();
}

VARAModGUI::VARAModGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSource *txChannel, QWidget* parent) :
    ChannelGUI(parent),
    ui(new Ui::VARAModGUI),
    m_pluginAPI(pluginAPI),
    m_deviceUISet(deviceUISet),
    m_channelMarker(this),
    m_deviceCenterFrequency(0),
    m_basebandSampleRate(1),
    m_doApplySettings(true),
    m_tickCount(0),
    m_engineMode(VARAEngine::FMNarrow)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/channeltx/modvara/readme.md";
    RollupContents *rollupContents = getRollupContents();
    ui->setupUi(rollupContents);
    setSizePolicy(rollupContents->sizePolicy());
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));

    m_varaMod = reinterpret_cast<VARAMod*>(txChannel);
    m_varaMod->setMessageQueueToGUI(getInputMessageQueue());

    connect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick()));

    ui->deltaFrequencyLabel->setText(QString("%1f").arg(QChar(0x94, 0x03)));
    ui->deltaFrequency->setColorMapper(ColorMapper(ColorMapper::GrayGold));
    ui->deltaFrequency->setValueRange(false, 7, -9999999, 9999999);

    m_channelMarker.blockSignals(true);
    m_channelMarker.setColor(QColor::fromRgb(m_settings.m_rgbColor));
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setTitle("VARA Modulator");
    m_channelMarker.setSourceOrSinkStream(false);
    m_channelMarker.blockSignals(false);
    m_channelMarker.setVisible(true); // activate signal on the last setting only

    m_settings.setChannelMarker(&m_channelMarker);
    m_settings.setRollupState(&m_rollupState);

    m_deviceUISet->addChannelMarker(&m_channelMarker);

    connect(&m_channelMarker, SIGNAL(changedByCursor()), this, SLOT(channelMarkerChangedByCursor()));
    connect(&m_channelMarker, SIGNAL(highlightedByCursor()), this, SLOT(channelMarkerHighlightedByCursor()));
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));

    m_varaMod->setLevelMeter(ui->volumeMeter);

    displaySettings();
    makeUIConnections();
    applySettings(QStringList(), true);
    DialPopup::addPopupsToChildDials(this);
    m_resizer.enableChildMouseTracking();
}

VARAModGUI::~VARAModGUI()
{
    delete ui;
}

void VARAModGUI::applySettings(const QStringList& settingsKeys, bool force)
{
    if (m_doApplySettings)
    {
        VARAMod::MsgConfigureVARAMod* message = VARAMod::MsgConfigureVARAMod::create(settingsKeys, m_settings, force);
        m_varaMod->getInputMessageQueue()->push(message);

        // Changes received from linked channels are displayed with applySettings blocked, so are not sent back
        if (m_settings.m_linkFrequencyOffset
            && (settingsKeys.contains("inputFrequencyOffset") || settingsKeys.contains("linkFrequencyOffset")))
        {
            ChannelWebAPIUtils::setLinkedFrequencyOffsets(m_varaMod, QStringList{"sdrangel.channel.varademod", "sdrangel.channeltx.modvara"}, m_settings.m_inputFrequencyOffset);
        }
    }
}

void VARAModGUI::displaySettings()
{
    m_channelMarker.blockSignals(true);
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setTitle(m_settings.m_title);
    m_channelMarker.blockSignals(false);
    m_channelMarker.setColor(m_settings.m_rgbColor); // activate signal on the last setting only

    setTitleColor(m_settings.m_rgbColor);
    setWindowTitle(m_channelMarker.getTitle());
    setTitle(m_channelMarker.getTitle());

    blockSignals(true);
    m_doApplySettings = false;

    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    ui->channelMute->setChecked(m_settings.m_channelMute);
    ui->cmdPort->setValue(m_settings.m_cmdPort);
    ui->fmDeviation->setValue(std::round(m_settings.m_fmDeviation));
    ui->gain->setValue(std::round(m_settings.m_gain));
    ui->linkFrequencyOffset->setChecked(m_settings.m_linkFrequencyOffset);
    ui->pttEnabled->setChecked(m_settings.m_pttEnabled);
    ui->pttAddress->setText(m_settings.m_pttAddress);
    ui->pttPort->setValue(m_settings.m_pttPort);
    ui->pttLeadMs->setValue(m_settings.m_pttLeadMs);
    ui->pttTailMs->setValue(m_settings.m_pttTailMs);
    ui->txPathMs->setValue(m_settings.m_txPathMs);
    displayMarker();

    updateIndexLabel();
    getRollupContents()->restoreState(m_rollupState);
    updateAbsoluteCenterFrequency();

    m_doApplySettings = true;
    blockSignals(false);
}

void VARAModGUI::displayMarker()
{
    // The Demod owns the modem, so the mode is the one of the modem it transmits
    bool fm = VARAEngine::isFM(m_engineMode);
    ui->fmDeviation->setEnabled(fm);

    if (fm)
    {
        m_channelMarker.setSidebands(ChannelMarker::dsb);
        m_channelMarker.setBandwidth(m_engineMode == VARAEngine::FMWide ? 24000 : 13000);
    }
    else
    {
        m_channelMarker.setSidebands(ChannelMarker::usb);
        m_channelMarker.setBandwidth(3100);
    }
}

void VARAModGUI::displayStatus()
{
    VARAMod::Status status = m_varaMod->getStatus();

    if (!status.m_linked) {
        ui->modem->setText(m_settings.m_channelMute ? tr("Muted") : tr("Not running"));
    } else if (!status.m_engineAttached) {
        ui->modem->setText(tr("No VARA Demod on port %1").arg(m_settings.m_cmdPort));
    } else {
        ui->modem->setText(VARAEngine::getModeName(status.m_engineMode));
    }

    ui->tx->setText(tr("%1 %2 ms").arg(status.m_keyed ? "Keyed" : "Idle").arg(status.m_fifoMs));
    ui->underflows->setText(QString::number(status.m_underflows));
    ui->pttReply->setText(status.m_pttReply.isEmpty() ? "-" : status.m_pttReply);
    setStatusText(status.m_keyed ? "TX" : "");

    if (status.m_engineAttached && (status.m_engineMode != m_engineMode))
    {
        m_engineMode = status.m_engineMode;
        displayMarker();
    }
}

void VARAModGUI::tick()
{
    double powDb = CalcDb::dbPower(m_varaMod->getMagSq());

    if (m_tickCount % 4 == 0)
    {
        ui->channelPower->setText(QString::number(powDb, 'f', 1));
        displayStatus();
    }

    m_tickCount++;
}

void VARAModGUI::makeUIConnections()
{
    QObject::connect(ui->deltaFrequency, &ValueDialZ::changed, this, &VARAModGUI::on_deltaFrequency_changed);
    QObject::connect(ui->channelMute, &QToolButton::toggled, this, &VARAModGUI::on_channelMute_toggled);
    QObject::connect(ui->cmdPort, &QSpinBox::editingFinished, this, &VARAModGUI::on_cmdPort_editingFinished);
    QObject::connect(ui->fmDeviation, qOverload<int>(&QSpinBox::valueChanged), this, &VARAModGUI::on_fmDeviation_valueChanged);
    QObject::connect(ui->gain, qOverload<int>(&QSpinBox::valueChanged), this, &VARAModGUI::on_gain_valueChanged);
    QObject::connect(ui->linkFrequencyOffset, &ButtonSwitch::toggled, this, &VARAModGUI::on_linkFrequencyOffset_toggled);
    QObject::connect(ui->pttEnabled, &ButtonSwitch::toggled, this, &VARAModGUI::on_pttEnabled_toggled);
    QObject::connect(ui->pttAddress, &QLineEdit::editingFinished, this, &VARAModGUI::on_pttAddress_editingFinished);
    QObject::connect(ui->pttPort, &QSpinBox::editingFinished, this, &VARAModGUI::on_pttPort_editingFinished);
    QObject::connect(ui->pttQuery, &QToolButton::clicked, this, &VARAModGUI::on_pttQuery_clicked);
    QObject::connect(ui->pttLeadMs, qOverload<int>(&QSpinBox::valueChanged), this, &VARAModGUI::on_pttLeadMs_valueChanged);
    QObject::connect(ui->pttTailMs, qOverload<int>(&QSpinBox::valueChanged), this, &VARAModGUI::on_pttTailMs_valueChanged);
    QObject::connect(ui->txPathMs, qOverload<int>(&QSpinBox::valueChanged), this, &VARAModGUI::on_txPathMs_valueChanged);
}

void VARAModGUI::updateAbsoluteCenterFrequency()
{
    setStatusFrequency(m_deviceCenterFrequency + m_settings.m_inputFrequencyOffset);
}
