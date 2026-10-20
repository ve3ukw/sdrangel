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
#include <QEvent>
#include <QFileDialog>
#include <QPainter>
#include <QPixmap>

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

#include "ui_sstvdemodgui.h"
#include "sstvmodes.h"
#include "sstvdemodreport.h"
#include "sstvdemod.h"
#include "sstvdemodgui.h"

SSTVDemodGUI* SSTVDemodGUI::create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel)
{
    SSTVDemodGUI* gui = new SSTVDemodGUI(pluginAPI, deviceUISet, rxChannel);
    return gui;
}

void SSTVDemodGUI::destroy()
{
    delete this;
}

void SSTVDemodGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applySettings(QStringList(), true);
}

QByteArray SSTVDemodGUI::serialize() const
{
    return m_settings.serialize();
}

bool SSTVDemodGUI::deserialize(const QByteArray& data)
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

bool SSTVDemodGUI::handleMessage(const Message& message)
{
    if (SSTVDemod::MsgConfigureSSTVDemod::match(message))
    {
        qDebug("SSTVDemodGUI::handleMessage: SSTVDemod::MsgConfigureSSTVDemod");
        const SSTVDemod::MsgConfigureSSTVDemod& cfg = (const SSTVDemod::MsgConfigureSSTVDemod&) message;

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
    else if (SSTVDemodReport::MsgImageStarted::match(message))
    {
        const SSTVDemodReport::MsgImageStarted& report = (const SSTVDemodReport::MsgImageStarted&) message;
        const SSTVMode& mode = SSTVMode::getMode(report.getModeIndex());
        m_image = QImage(mode.m_width, mode.m_height, QImage::Format_RGB32);
        m_image.fill(Qt::black);
        m_statusMode = report.getVisCode() >= 0
            ? tr("%1 (VIS %2)").arg(mode.m_name).arg(report.getVisCode())
            : tr("%1 (sync)").arg(mode.m_name);
        setStatus(tr("%1 0/%2").arg(m_statusMode).arg(mode.m_height));
        updateImage();
        return true;
    }
    else if (SSTVDemodReport::MsgLines::match(message))
    {
        const SSTVDemodReport::MsgLines& report = (const SSTVDemodReport::MsgLines&) message;

        if (!m_image.isNull())
        {
            QPainter painter(&m_image);
            painter.drawImage(0, report.getFirstRow(), report.getRows());
            painter.end();
            setStatus(tr("%1 %2/%3 %4 ppm")
                .arg(m_statusMode)
                .arg(report.getRowsDone())
                .arg(m_image.height())
                .arg(report.getClockErrorPpm(), 0, 'f', 0));
            updateImage();
        }

        return true;
    }
    else if (SSTVDemodReport::MsgImageFinished::match(message))
    {
        const SSTVDemodReport::MsgImageFinished& report = (const SSTVDemodReport::MsgImageFinished&) message;
        m_image = report.getImage();
        setStatus(tr("%1 %2 - waiting").arg(m_statusMode).arg(report.getComplete() ? tr("complete") : tr("incomplete")));
        updateImage();
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

void SSTVDemodGUI::handleInputMessages()
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

void SSTVDemodGUI::setStatus(const QString& status)
{
    ui->status->setText(status);
    setStatusText(status);
}

void SSTVDemodGUI::updateImage()
{
    if (m_image.isNull())
    {
        ui->image->clear();
        return;
    }

    ui->image->setPixmap(QPixmap::fromImage(m_image).scaled(
        ui->image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

bool SSTVDemodGUI::eventFilter(QObject *obj, QEvent *event)
{
    if ((obj == ui->image) && (event->type() == QEvent::Resize)) {
        updateImage();
    }

    return ChannelGUI::eventFilter(obj, event);
}

void SSTVDemodGUI::channelMarkerChangedByCursor()
{
    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void SSTVDemodGUI::channelMarkerHighlightedByCursor()
{
    setHighlighted(m_channelMarker.getHighlighted());
}

void SSTVDemodGUI::on_deltaFrequency_changed(qint64 value)
{
    m_channelMarker.setCenterFrequency(value);
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    updateAbsoluteCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void SSTVDemodGUI::on_rfBW_valueChanged(int value)
{
    ui->rfBWText->setText(QString("%1k").arg(value / 10.0, 0, 'f', 1));
    m_settings.m_rfBandwidth = value * 100.0f;
    m_channelMarker.setBandwidth(m_settings.m_rfBandwidth);
    applySettings(QStringList("rfBandwidth"));
}

void SSTVDemodGUI::on_fmDev_valueChanged(int value)
{
    ui->fmDevText->setText(QString("%1k").arg(value / 10.0, 0, 'f', 1));
    m_settings.m_fmDeviation = value * 100.0f;
    applySettings(QStringList("fmDeviation"));
}

void SSTVDemodGUI::on_volume_valueChanged(int value)
{
    ui->volumeText->setText(QString("%1").arg(value));
    m_settings.m_volume = value / 100.0f;
    applySettings(QStringList("volume"));
}

void SSTVDemodGUI::on_audioMute_toggled(bool checked)
{
    m_settings.m_audioMute = checked;
    applySettings(QStringList("audioMute"));
}

void SSTVDemodGUI::on_mode_currentIndexChanged(int index)
{
    const int mode = ui->mode->itemData(index).toInt();
    m_settings.m_autoMode = mode < 0;

    if (mode >= 0) {
        m_settings.m_mode = mode;
    }

    applySettings(QStringList({"autoMode", "mode"}));
}

void SSTVDemodGUI::on_autoSave_toggled(bool checked)
{
    m_settings.m_autoSave = checked;
    applySettings(QStringList("autoSave"));
}

void SSTVDemodGUI::on_autoSavePath_editingFinished()
{
    m_settings.m_autoSavePath = ui->autoSavePath->text().trimmed();
    applySettings(QStringList("autoSavePath"));
}

void SSTVDemodGUI::on_autoSavePathBrowse_clicked()
{
    QString directory = QFileDialog::getExistingDirectory(
        this,
        tr("Select folder for SSTV images"),
        m_settings.m_autoSavePath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!directory.isEmpty())
    {
        m_settings.m_autoSavePath = directory;
        ui->autoSavePath->setText(directory);
        applySettings(QStringList("autoSavePath"));
    }
}

void SSTVDemodGUI::on_saveImage_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Save SSTV image"),
        m_settings.m_autoSavePath,
        tr("PNG images (*.png);;JPEG images (*.jpg)"));

    if (!fileName.isEmpty() && !m_sstvDemod->saveImage(fileName)) {
        setStatus(tr("Could not save %1").arg(fileName));
    }
}

void SSTVDemodGUI::on_clearImage_clicked()
{
    m_sstvDemod->getInputMessageQueue()->push(SSTVDemod::MsgClear::create());
    m_image = QImage();
    setStatus(tr("Waiting"));
    updateImage();
}

void SSTVDemodGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;

    getRollupContents()->saveState(m_rollupState);
    applySettings(QStringList("rollupState"));
}

void SSTVDemodGUI::onMenuDialogCalled(const QPoint &p)
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
            dialog.setNumberOfStreams(m_sstvDemod->getNumberOfDeviceStreams());
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

SSTVDemodGUI::SSTVDemodGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel, QWidget* parent) :
    ChannelGUI(parent),
    ui(new Ui::SSTVDemodGUI),
    m_pluginAPI(pluginAPI),
    m_deviceUISet(deviceUISet),
    m_channelMarker(this),
    m_deviceCenterFrequency(0),
    m_basebandSampleRate(1),
    m_doApplySettings(true),
    m_tickCount(0)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/channelrx/demodsstv/readme.md";
    RollupContents *rollupContents = getRollupContents();
    ui->setupUi(rollupContents);
    setSizePolicy(rollupContents->sizePolicy());
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));

    m_sstvDemod = reinterpret_cast<SSTVDemod*>(rxChannel);
    m_sstvDemod->setMessageQueueToGUI(getInputMessageQueue());

    connect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick()));

    CRightClickEnabler *audioMuteRightClickEnabler = new CRightClickEnabler(ui->audioMute);
    connect(audioMuteRightClickEnabler, SIGNAL(rightClick(const QPoint &)), this, SLOT(audioSelect(const QPoint &)));

    ui->mode->addItem(tr("Auto"), -1);

    for (int i = 0; i < SSTVMode::NbModes; i++) {
        ui->mode->addItem(SSTVMode::getMode(i).m_name, i);
    }

    ui->deltaFrequencyLabel->setText(QString("%1f").arg(QChar(0x94, 0x03)));
    ui->deltaFrequency->setColorMapper(ColorMapper(ColorMapper::GrayGold));
    ui->deltaFrequency->setValueRange(false, 7, -9999999, 9999999);
    ui->channelPowerMeter->setColorTheme(LevelMeterSignalDB::ColorGreenAndBlue);
    ui->image->installEventFilter(this);

    m_channelMarker.blockSignals(true);
    m_channelMarker.setColor(QColor::fromRgb(m_settings.m_rgbColor));
    m_channelMarker.setBandwidth(m_settings.m_rfBandwidth);
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setTitle("SSTV Demodulator");
    m_channelMarker.blockSignals(false);
    m_channelMarker.setVisible(true); // activate signal on the last setting only

    m_settings.setChannelMarker(&m_channelMarker);
    m_settings.setRollupState(&m_rollupState);

    m_deviceUISet->addChannelMarker(&m_channelMarker);

    connect(&m_channelMarker, SIGNAL(changedByCursor()), this, SLOT(channelMarkerChangedByCursor()));
    connect(&m_channelMarker, SIGNAL(highlightedByCursor()), this, SLOT(channelMarkerHighlightedByCursor()));
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));

    // Show an image that was received while the GUI was closed
    m_image = m_sstvDemod->getImage();

    if (m_sstvDemod->isReceiving() && (m_sstvDemod->getModeIndex() >= 0))
    {
        const SSTVMode& mode = SSTVMode::getMode(m_sstvDemod->getModeIndex());
        m_statusMode = m_sstvDemod->getVisCode() >= 0
            ? tr("%1 (VIS %2)").arg(mode.m_name).arg(m_sstvDemod->getVisCode())
            : tr("%1 (sync)").arg(mode.m_name);
        setStatus(tr("%1 %2/%3").arg(m_statusMode).arg(m_sstvDemod->getRowsDone()).arg(mode.m_height));
    }
    else
    {
        setStatus(tr("Waiting"));
    }

    displaySettings();
    makeUIConnections();
    applySettings(QStringList(), true);
    DialPopup::addPopupsToChildDials(this);
    m_resizer.enableChildMouseTracking();
}

SSTVDemodGUI::~SSTVDemodGUI()
{
    delete ui;
}

void SSTVDemodGUI::applySettings(const QStringList& settingsKeys, bool force)
{
    if (m_doApplySettings)
    {
        SSTVDemod::MsgConfigureSSTVDemod* message = SSTVDemod::MsgConfigureSSTVDemod::create(settingsKeys, m_settings, force);
        m_sstvDemod->getInputMessageQueue()->push(message);
    }
}

void SSTVDemodGUI::displaySettings()
{
    m_channelMarker.blockSignals(true);
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setBandwidth(m_settings.m_rfBandwidth);
    m_channelMarker.setTitle(m_settings.m_title);
    m_channelMarker.blockSignals(false);
    m_channelMarker.setColor(m_settings.m_rgbColor); // activate signal on the last setting only

    setTitleColor(m_settings.m_rgbColor);
    setWindowTitle(m_channelMarker.getTitle());
    setTitle(m_channelMarker.getTitle());

    blockSignals(true);
    m_doApplySettings = false;

    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    ui->rfBW->setValue(std::round(m_settings.m_rfBandwidth / 100.0));
    ui->rfBWText->setText(QString("%1k").arg(m_settings.m_rfBandwidth / 1000.0, 0, 'f', 1));
    ui->fmDev->setValue(std::round(m_settings.m_fmDeviation / 100.0));
    ui->fmDevText->setText(QString("%1k").arg(m_settings.m_fmDeviation / 1000.0, 0, 'f', 1));
    ui->volume->setValue(std::round(m_settings.m_volume * 100.0));
    ui->volumeText->setText(QString("%1").arg(m_settings.m_volume * 100.0, 0, 'f', 0));
    ui->audioMute->setChecked(m_settings.m_audioMute);
    ui->mode->setCurrentIndex(m_settings.m_autoMode ? 0 : ui->mode->findData(m_settings.m_mode));
    ui->autoSave->setChecked(m_settings.m_autoSave);
    ui->autoSavePath->setText(m_settings.m_autoSavePath);

    updateIndexLabel();
    getRollupContents()->restoreState(m_rollupState);
    updateAbsoluteCenterFrequency();

    m_doApplySettings = true;
    blockSignals(false);
}

void SSTVDemodGUI::audioSelect(const QPoint& p)
{
    qDebug("SSTVDemodGUI::audioSelect");
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

void SSTVDemodGUI::tick()
{
    double magsqAvg, magsqPeak;
    int nbMagsqSamples;
    m_sstvDemod->getMagSqLevels(magsqAvg, magsqPeak, nbMagsqSamples);
    double powDbAvg = CalcDb::dbPower(magsqAvg);
    double powDbPeak = CalcDb::dbPower(magsqPeak);

    ui->channelPowerMeter->levelChanged(
            (100.0f + powDbAvg) / 100.0f,
            (100.0f + powDbPeak) / 100.0f,
            nbMagsqSamples);

    if (m_tickCount % 4 == 0) {
        ui->channelPower->setText(QString::number(powDbAvg, 'f', 1));
    }

    m_tickCount++;
}

void SSTVDemodGUI::makeUIConnections()
{
    QObject::connect(ui->deltaFrequency, &ValueDialZ::changed, this, &SSTVDemodGUI::on_deltaFrequency_changed);
    QObject::connect(ui->rfBW, &QSlider::valueChanged, this, &SSTVDemodGUI::on_rfBW_valueChanged);
    QObject::connect(ui->fmDev, &QSlider::valueChanged, this, &SSTVDemodGUI::on_fmDev_valueChanged);
    QObject::connect(ui->volume, &QDial::valueChanged, this, &SSTVDemodGUI::on_volume_valueChanged);
    QObject::connect(ui->audioMute, &QToolButton::toggled, this, &SSTVDemodGUI::on_audioMute_toggled);
    QObject::connect(ui->mode, qOverload<int>(&QComboBox::currentIndexChanged), this, &SSTVDemodGUI::on_mode_currentIndexChanged);
    QObject::connect(ui->autoSave, &ButtonSwitch::toggled, this, &SSTVDemodGUI::on_autoSave_toggled);
    QObject::connect(ui->autoSavePath, &QLineEdit::editingFinished, this, &SSTVDemodGUI::on_autoSavePath_editingFinished);
    QObject::connect(ui->autoSavePathBrowse, &QToolButton::clicked, this, &SSTVDemodGUI::on_autoSavePathBrowse_clicked);
    QObject::connect(ui->saveImage, &QToolButton::clicked, this, &SSTVDemodGUI::on_saveImage_clicked);
    QObject::connect(ui->clearImage, &QToolButton::clicked, this, &SSTVDemodGUI::on_clearImage_clicked);
}

void SSTVDemodGUI::updateAbsoluteCenterFrequency()
{
    setStatusFrequency(m_deviceCenterFrequency + m_settings.m_inputFrequencyOffset);
}
