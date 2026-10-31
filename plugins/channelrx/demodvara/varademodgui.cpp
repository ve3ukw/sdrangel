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
#include "dsp/dspengine.h"
#include "dsp/dspcommands.h"
#include "gui/basicchannelsettingsdialog.h"
#include "gui/crightclickenabler.h"
#include "gui/audioselectdialog.h"
#include "gui/dialpopup.h"
#include "gui/dialogpositioner.h"
#include "plugin/pluginapi.h"
#include "util/db.h"
#include "maincore.h"

#include "ui_varademodgui.h"
#include "varaengine.h"
#include "varademodsink.h"
#include "varademod.h"
#include "varademodgui.h"

VARADemodGUI* VARADemodGUI::create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel)
{
    VARADemodGUI* gui = new VARADemodGUI(pluginAPI, deviceUISet, rxChannel);
    return gui;
}

void VARADemodGUI::destroy()
{
    delete this;
}

void VARADemodGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applySettings(QStringList(), true);
}

QByteArray VARADemodGUI::serialize() const
{
    return m_settings.serialize();
}

bool VARADemodGUI::deserialize(const QByteArray& data)
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

bool VARADemodGUI::handleMessage(const Message& message)
{
    if (VARADemod::MsgConfigureVARADemod::match(message))
    {
        qDebug("VARADemodGUI::handleMessage: VARADemod::MsgConfigureVARADemod");
        const VARADemod::MsgConfigureVARADemod& cfg = (const VARADemod::MsgConfigureVARADemod&) message;

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

void VARADemodGUI::handleInputMessages()
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

void VARADemodGUI::channelMarkerChangedByCursor()
{
    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void VARADemodGUI::channelMarkerHighlightedByCursor()
{
    setHighlighted(m_channelMarker.getHighlighted());
}

void VARADemodGUI::on_deltaFrequency_changed(qint64 value)
{
    m_channelMarker.setCenterFrequency(value);
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    updateAbsoluteCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void VARADemodGUI::on_audioMute_toggled(bool checked)
{
    m_settings.m_audioMute = checked;
    applySettings(QStringList("audioMute"));
}

void VARADemodGUI::on_mode_currentIndexChanged(int index)
{
    m_settings.m_mode = ui->mode->itemData(index).toInt();
    displayModeControls();
    applySettings(QStringList("mode"));
}

void VARADemodGUI::on_callsign_editingFinished()
{
    QString callsign = ui->callsign->text().trimmed().toUpper();
    ui->callsign->setText(callsign);

    if (callsign != m_settings.m_callsign)
    {
        m_settings.m_callsign = callsign;
        applySettings(QStringList("callsign"));
    }
}

void VARADemodGUI::on_linkFrequencyOffset_toggled(bool checked)
{
    m_settings.m_linkFrequencyOffset = checked;
    applySettings(QStringList("linkFrequencyOffset"));
}

void VARADemodGUI::on_cmdPort_editingFinished()
{
    if (ui->cmdPort->value() != m_settings.m_cmdPort)
    {
        m_settings.m_cmdPort = ui->cmdPort->value();
        applySettings(QStringList("cmdPort"));
    }
}

void VARADemodGUI::on_kissEnabled_toggled(bool checked)
{
    m_settings.m_kissEnabled = checked;
    ui->kissPort->setEnabled(checked);
    applySettings(QStringList("kissEnabled"));
}

void VARADemodGUI::on_kissPort_editingFinished()
{
    if (ui->kissPort->value() != m_settings.m_kissPort)
    {
        m_settings.m_kissPort = ui->kissPort->value();
        applySettings(QStringList("kissPort"));
    }
}

void VARADemodGUI::on_address_editingFinished()
{
    QString address = ui->address->text().trimmed();

    if (address != m_settings.m_address)
    {
        m_settings.m_address = address;
        applySettings(QStringList("address"));
    }
}

void VARADemodGUI::on_driveDb_valueChanged(int value)
{
    m_settings.m_driveDb = value;
    applySettings(QStringList("driveDb"));
}

void VARADemodGUI::on_rxLagMs_valueChanged(int value)
{
    m_settings.m_rxLagMs = value;
    applySettings(QStringList("rxLagMs"));
}

void VARADemodGUI::on_fmDeviation_valueChanged(int value)
{
    m_settings.m_fmDeviation = value;
    applySettings(QStringList("fmDeviation"));
}

void VARADemodGUI::on_agc_toggled(bool checked)
{
    m_settings.m_agc = checked;
    displayModeControls();
    applySettings(QStringList("agc"));
}

void VARADemodGUI::on_rxGainDb_valueChanged(int value)
{
    m_settings.m_rxGainDb = value;
    applySettings(QStringList("rxGainDb"));
}

void VARADemodGUI::on_volume_valueChanged(int value)
{
    m_settings.m_volume = value / 100.0f;
    applySettings(QStringList("volume"));
}

void VARADemodGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;

    getRollupContents()->saveState(m_rollupState);
    applySettings(QStringList("rollupState"));
}

void VARADemodGUI::onMenuDialogCalled(const QPoint &p)
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
            dialog.setNumberOfStreams(m_varaDemod->getNumberOfDeviceStreams());
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

VARADemodGUI::VARADemodGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel, QWidget* parent) :
    ChannelGUI(parent),
    ui(new Ui::VARADemodGUI),
    m_pluginAPI(pluginAPI),
    m_deviceUISet(deviceUISet),
    m_channelMarker(this),
    m_deviceCenterFrequency(0),
    m_basebandSampleRate(1),
    m_doApplySettings(true),
    m_tickCount(0)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/channelrx/demodvara/readme.md";
    RollupContents *rollupContents = getRollupContents();
    ui->setupUi(rollupContents);
    setSizePolicy(rollupContents->sizePolicy());
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));

    m_varaDemod = reinterpret_cast<VARADemod*>(rxChannel);
    m_varaDemod->setMessageQueueToGUI(getInputMessageQueue());

    connect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick()));

    CRightClickEnabler *audioMuteRightClickEnabler = new CRightClickEnabler(ui->audioMute);
    connect(audioMuteRightClickEnabler, SIGNAL(rightClick(const QPoint &)), this, SLOT(audioSelect(const QPoint &)));

    for (int mode : {VARAEngine::FMNarrow, VARAEngine::FMWide, VARAEngine::HF500, VARAEngine::HF2300, VARAEngine::HF2750}) {
        ui->mode->addItem(VARAEngine::getModeName(mode), mode);
    }

    ui->deltaFrequencyLabel->setText(QString("%1f").arg(QChar(0x94, 0x03)));
    ui->deltaFrequency->setColorMapper(ColorMapper(ColorMapper::GrayGold));
    ui->deltaFrequency->setValueRange(false, 7, -9999999, 9999999);
    ui->channelPowerMeter->setColorTheme(LevelMeterSignalDB::ColorGreenAndBlue);
    ui->modemLevelMeter->setColorTheme(LevelMeterSignalDB::ColorCyanAndBlue);
    ui->modemLevelMeter->setRange(-60.0, 0.0);

    m_channelMarker.blockSignals(true);
    m_channelMarker.setColor(QColor::fromRgb(m_settings.m_rgbColor));
    m_channelMarker.setBandwidth(VARADemodSink::getRFBandwidth(m_settings.m_mode));
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setTitle("VARA Demodulator");
    m_channelMarker.blockSignals(false);
    m_channelMarker.setVisible(true); // activate signal on the last setting only

    m_settings.setChannelMarker(&m_channelMarker);
    m_settings.setRollupState(&m_rollupState);

    m_deviceUISet->addChannelMarker(&m_channelMarker);

    connect(&m_channelMarker, SIGNAL(changedByCursor()), this, SLOT(channelMarkerChangedByCursor()));
    connect(&m_channelMarker, SIGNAL(highlightedByCursor()), this, SLOT(channelMarkerHighlightedByCursor()));
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));

    displaySettings();
    makeUIConnections();
    applySettings(QStringList(), true);
    DialPopup::addPopupsToChildDials(this);
    m_resizer.enableChildMouseTracking();
}

VARADemodGUI::~VARADemodGUI()
{
    delete ui;
}

void VARADemodGUI::applySettings(const QStringList& settingsKeys, bool force)
{
    if (m_doApplySettings)
    {
        VARADemod::MsgConfigureVARADemod* message = VARADemod::MsgConfigureVARADemod::create(settingsKeys, m_settings, force);
        m_varaDemod->getInputMessageQueue()->push(message);

        // Changes received from linked channels are displayed with applySettings blocked, so are not sent back
        if (m_settings.m_linkFrequencyOffset
            && (settingsKeys.contains("inputFrequencyOffset") || settingsKeys.contains("linkFrequencyOffset")))
        {
            ChannelWebAPIUtils::setLinkedFrequencyOffsets(m_varaDemod, QStringList{"sdrangel.channel.varademod", "sdrangel.channeltx.modvara"}, m_settings.m_inputFrequencyOffset);
        }
    }
}

void VARADemodGUI::displaySettings()
{
    m_channelMarker.blockSignals(true);
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setBandwidth(VARADemodSink::getRFBandwidth(m_settings.m_mode));
    m_channelMarker.setTitle(m_settings.m_title);
    m_channelMarker.blockSignals(false);
    m_channelMarker.setColor(m_settings.m_rgbColor); // activate signal on the last setting only

    setTitleColor(m_settings.m_rgbColor);
    setWindowTitle(m_channelMarker.getTitle());
    setTitle(m_channelMarker.getTitle());

    blockSignals(true);
    m_doApplySettings = false;

    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    ui->audioMute->setChecked(m_settings.m_audioMute);
    ui->mode->setCurrentIndex(std::max(0, ui->mode->findData(m_settings.m_mode)));
    ui->callsign->setText(m_settings.m_callsign);
    ui->linkFrequencyOffset->setChecked(m_settings.m_linkFrequencyOffset);
    ui->cmdPort->setValue(m_settings.m_cmdPort);
    ui->kissEnabled->setChecked(m_settings.m_kissEnabled);
    ui->kissPort->setValue(m_settings.m_kissPort);
    ui->kissPort->setEnabled(m_settings.m_kissEnabled);
    ui->address->setText(m_settings.m_address);
    ui->driveDb->setValue(std::round(m_settings.m_driveDb));
    ui->rxLagMs->setValue(m_settings.m_rxLagMs);
    ui->fmDeviation->setValue(std::round(m_settings.m_fmDeviation));
    ui->agc->setChecked(m_settings.m_agc);
    ui->rxGainDb->setValue(std::round(m_settings.m_rxGainDb));
    ui->volume->setValue(std::round(m_settings.m_volume * 100.0));
    displayModeControls();

    updateIndexLabel();
    getRollupContents()->restoreState(m_rollupState);
    updateAbsoluteCenterFrequency();

    m_doApplySettings = true;
    blockSignals(false);
}

void VARADemodGUI::displayModeControls()
{
    bool fm = VARAEngine::isFM(m_settings.m_mode);
    ui->fmDeviation->setEnabled(fm);
    ui->agc->setEnabled(!fm);
    ui->rxGainDb->setEnabled(!fm && !m_settings.m_agc);
    m_channelMarker.setBandwidth(VARADemodSink::getRFBandwidth(m_settings.m_mode));

    if (fm) {
        m_channelMarker.setSidebands(ChannelMarker::dsb);
    } else {
        m_channelMarker.setSidebands(ChannelMarker::usb);
    }
}

void VARADemodGUI::displayStatus()
{
    VARAEngine::Status status = m_varaDemod->getEngineStatus();

    if (!status.m_running)
    {
        ui->state->setText("Stopped");
        ui->peer->setText("-");
        ui->bandwidth->setText("-");
        ui->afc->setText("-");
        ui->ports->setText("-");
        ui->flags->setText("-");
        ui->error->setText(status.m_error);
        setStatusText(status.m_error);
        return;
    }

    ui->state->setText(status.m_state);
    ui->peer->setText(status.m_peer.isEmpty() ? "-" : status.m_peer);
    ui->bandwidth->setText(status.m_bandwidth.isEmpty() ? "-" : tr("%1 level %2").arg(status.m_bandwidth).arg(status.m_level));
    ui->afc->setText(std::isnan(status.m_afcHz) ? "-" : tr("%1 Hz").arg(status.m_afcHz, 0, 'f', 1));
    ui->ports->setText(status.m_kissPort ? tr("%1/%2 KISS %3").arg(status.m_cmdPort).arg(status.m_dataPort).arg(status.m_kissPort)
                                         : tr("%1/%2").arg(status.m_cmdPort).arg(status.m_dataPort));
    QStringList flags;

    if (status.m_keyed) {
        flags.append("TX");
    }
    if (status.m_busy) {
        flags.append("BUSY");
    }
    if (status.m_tuning) {
        flags.append("TUNE");
    }

    flags.append(status.m_txLinked ? "Mod" : "no Mod");
    ui->flags->setText(flags.join(" "));
    ui->error->setText(status.m_txLinked ? "" : tr("No VARA Modulator on port %1, so the modem can't transmit").arg(status.m_cmdPort));
    setStatusText(status.m_peer.isEmpty() ? status.m_state : tr("%1 %2").arg(status.m_state).arg(status.m_peer));
}

void VARADemodGUI::audioSelect(const QPoint& p)
{
    qDebug("VARADemodGUI::audioSelect");
    AudioSelectDialog audioSelect(DSPEngine::instance()->getAudioDeviceManager(), m_settings.m_audioDeviceName);
    audioSelect.move(p);
    new DialogPositioner(&audioSelect, false);
    audioSelect.exec();

    if (audioSelect.m_selected)
    {
        m_settings.m_audioDeviceName = audioSelect.m_audioDeviceName;
        applySettings(QStringList("audioDeviceName"));
    }
}

void VARADemodGUI::tick()
{
    double magsqAvg, magsqPeak;
    int nbMagsqSamples;
    m_varaDemod->getMagSqLevels(magsqAvg, magsqPeak, nbMagsqSamples);
    double powDbAvg = CalcDb::dbPower(magsqAvg);
    double powDbPeak = CalcDb::dbPower(magsqPeak);

    ui->channelPowerMeter->levelChanged(
            (100.0f + powDbAvg) / 100.0f,
            (100.0f + powDbPeak) / 100.0f,
            nbMagsqSamples);

    float modemRms, modemPeak;
    m_varaDemod->getModemLevels(modemRms, modemPeak);
    double modemDbAvg = CalcDb::dbPower(std::max(modemRms * modemRms, 1e-10f));
    double modemDbPeak = CalcDb::dbPower(std::max(modemPeak * modemPeak, 1e-10f));
    ui->modemLevelMeter->levelChanged((60.0f + modemDbAvg) / 60.0f, (60.0f + modemDbPeak) / 60.0f, 1);

    if (m_tickCount % 4 == 0)
    {
        ui->channelPower->setText(QString::number(powDbAvg, 'f', 1));
        displayStatus();
    }

    m_tickCount++;
}

void VARADemodGUI::makeUIConnections()
{
    QObject::connect(ui->deltaFrequency, &ValueDialZ::changed, this, &VARADemodGUI::on_deltaFrequency_changed);
    QObject::connect(ui->audioMute, &QToolButton::toggled, this, &VARADemodGUI::on_audioMute_toggled);
    QObject::connect(ui->mode, qOverload<int>(&QComboBox::currentIndexChanged), this, &VARADemodGUI::on_mode_currentIndexChanged);
    QObject::connect(ui->callsign, &QLineEdit::editingFinished, this, &VARADemodGUI::on_callsign_editingFinished);
    QObject::connect(ui->linkFrequencyOffset, &ButtonSwitch::toggled, this, &VARADemodGUI::on_linkFrequencyOffset_toggled);
    QObject::connect(ui->cmdPort, &QSpinBox::editingFinished, this, &VARADemodGUI::on_cmdPort_editingFinished);
    QObject::connect(ui->kissEnabled, &ButtonSwitch::toggled, this, &VARADemodGUI::on_kissEnabled_toggled);
    QObject::connect(ui->kissPort, &QSpinBox::editingFinished, this, &VARADemodGUI::on_kissPort_editingFinished);
    QObject::connect(ui->address, &QLineEdit::editingFinished, this, &VARADemodGUI::on_address_editingFinished);
    QObject::connect(ui->driveDb, qOverload<int>(&QSpinBox::valueChanged), this, &VARADemodGUI::on_driveDb_valueChanged);
    QObject::connect(ui->rxLagMs, qOverload<int>(&QSpinBox::valueChanged), this, &VARADemodGUI::on_rxLagMs_valueChanged);
    QObject::connect(ui->fmDeviation, qOverload<int>(&QSpinBox::valueChanged), this, &VARADemodGUI::on_fmDeviation_valueChanged);
    QObject::connect(ui->agc, &ButtonSwitch::toggled, this, &VARADemodGUI::on_agc_toggled);
    QObject::connect(ui->rxGainDb, qOverload<int>(&QSpinBox::valueChanged), this, &VARADemodGUI::on_rxGainDb_valueChanged);
    QObject::connect(ui->volume, qOverload<int>(&QSpinBox::valueChanged), this, &VARADemodGUI::on_volume_valueChanged);
}

void VARADemodGUI::updateAbsoluteCenterFrequency()
{
    setStatusFrequency(m_deviceCenterFrequency + m_settings.m_inputFrequencyOffset);
}
