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
#include <QFileInfo>
#include <QImageReader>
#include <QPixmap>

#include "device/deviceuiset.h"
#include "dsp/dspcommands.h"
#include "gui/basicchannelsettingsdialog.h"
#include "gui/dialpopup.h"
#include "gui/dialogpositioner.h"
#include "plugin/pluginapi.h"
#include "util/db.h"
#include "util/ssdvcodec.h"
#include "maincore.h"

#include "ui_ssdvmodgui.h"
#include "ssdvmodreport.h"
#include "ssdvmod.h"
#include "ssdvmodgui.h"

SSDVModGUI* SSDVModGUI::create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSource *channelTx)
{
    SSDVModGUI* gui = new SSDVModGUI(pluginAPI, deviceUISet, channelTx);
    return gui;
}

void SSDVModGUI::destroy()
{
    delete this;
}

void SSDVModGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applySettings(QStringList(), true);
}

QByteArray SSDVModGUI::serialize() const
{
    return m_settings.serialize();
}

bool SSDVModGUI::deserialize(const QByteArray& data)
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

bool SSDVModGUI::handleMessage(const Message& message)
{
    if (SSDVMod::MsgConfigureSSDVMod::match(message))
    {
        const SSDVMod::MsgConfigureSSDVMod& cfg = (const SSDVMod::MsgConfigureSSDVMod&) message;

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
    else if (SSDVModReport::MsgImage::match(message))
    {
        const SSDVModReport::MsgImage& report = (const SSDVModReport::MsgImage&) message;

        if (!report.getError().isEmpty())
        {
            setStatus(report.getError());
            ui->tx->setEnabled(true);
        }
        else
        {
            m_image = report.getImage();
            updateImage();
            ui->progress->setMaximum(report.getPacketCount());
            ui->progress->setValue(0);
            setStatus(tr("Sending #%1 %2").arg(report.getImageId()).arg(QFileInfo(report.getFilename()).fileName()));
            ui->tx->setEnabled(false);
        }

        return true;
    }
    else if (SSDVModReport::MsgProgress::match(message))
    {
        const SSDVModReport::MsgProgress& report = (const SSDVModReport::MsgProgress&) message;
        ui->progress->setMaximum(std::max(1, report.getPacketCount()));
        ui->progress->setValue(report.getPacketsSent());

        if (report.getDone())
        {
            ui->tx->setEnabled(true);
            setStatus(report.getPacketsSent() >= report.getPacketCount() ? tr("Sent") : tr("Stopped"));
        }

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

void SSDVModGUI::handleInputMessages()
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

void SSDVModGUI::setStatus(const QString& status)
{
    ui->status->setText(status);
    setStatusText(status);
}

void SSDVModGUI::updateImage()
{
    if (m_image.isNull())
    {
        ui->image->clear();
        return;
    }

    ui->image->setPixmap(QPixmap::fromImage(m_image).scaled(
        ui->image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void SSDVModGUI::showPreview()
{
    // Show the first image as it would be sent, while not sending
    if (m_ssdvMod->isTransmitting()) {
        return;
    }

    QStringList files = SSDVMod::getImageFiles(m_settings.m_imageFilename);

    if (files.isEmpty())
    {
        m_image = QImage();
        setStatus(m_settings.m_imageFilename.isEmpty() ? tr("No image selected") : tr("No images found"));
    }
    else
    {
        QImage image(files.first());
        m_image = SSDV::Encoder::fit(image, std::max(16, m_settings.m_width & ~15), std::max(16, m_settings.m_height & ~15), m_settings.m_fill);
        setStatus(files.size() > 1 ? tr("%1 images").arg(files.size()) : tr("Idle"));
    }

    updateImage();
}

void SSDVModGUI::updateBandwidth()
{
    m_channelMarker.setBandwidth(m_settings.m_baudRate * (1.0f + m_settings.m_rrcRolloff));
}

bool SSDVModGUI::eventFilter(QObject *obj, QEvent *event)
{
    if ((obj == ui->image) && (event->type() == QEvent::Resize)) {
        updateImage();
    }

    return ChannelGUI::eventFilter(obj, event);
}

void SSDVModGUI::channelMarkerChangedByCursor()
{
    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void SSDVModGUI::channelMarkerHighlightedByCursor()
{
    setHighlighted(m_channelMarker.getHighlighted());
}

void SSDVModGUI::on_deltaFrequency_changed(qint64 value)
{
    m_channelMarker.setCenterFrequency(value);
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    updateAbsoluteCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void SSDVModGUI::on_channelMute_toggled(bool checked)
{
    m_settings.m_channelMute = checked;
    applySettings(QStringList("channelMute"));
}

void SSDVModGUI::on_gain_valueChanged(int value)
{
    m_settings.m_gain = value;
    applySettings(QStringList("gain"));
}

void SSDVModGUI::on_baudRate_currentIndexChanged(int index)
{
    m_settings.m_baudRate = ui->baudRate->itemText(index).toInt();
    updateBandwidth();
    applySettings(QStringList("baudRate"));
}

void SSDVModGUI::on_rrcRolloff_valueChanged(int value)
{
    m_settings.m_rrcRolloff = value / 100.0f;
    updateBandwidth();
    applySettings(QStringList("rrcRolloff"));
}

void SSDVModGUI::on_callsign_editingFinished()
{
    m_settings.m_callsign = ui->callsign->text().trimmed().toUpper();
    applySettings(QStringList("callsign"));
}

void SSDVModGUI::on_spacecraftId_valueChanged(int value)
{
    m_settings.m_spacecraftId = value;
    applySettings(QStringList("spacecraftId"));
}

void SSDVModGUI::on_virtualChannel_valueChanged(int value)
{
    m_settings.m_virtualChannel = value;
    applySettings(QStringList("virtualChannel"));
}

void SSDVModGUI::on_packetFormat_currentIndexChanged(int index)
{
    m_settings.m_packetFormat = index;

    if (m_doApplySettings && (index == SSDVModSettings::AO123))
    {
        // The coding AO-123 uses
        m_settings.m_frameSize = 223;
        m_settings.m_convolutional = true;
        m_settings.m_differential = true;
        m_settings.m_randomizer = true;
        m_settings.m_rs = true;
        m_settings.m_dualBasis = false;
        m_settings.m_virtualChannel = 1;
        displaySettings();
    }

    applySettings(QStringList({"packetFormat", "frameSize", "convolutional", "differential", "randomizer", "rs", "dualBasis", "virtualChannel"}));
}

void SSDVModGUI::on_frameSize_valueChanged(int value)
{
    m_settings.m_frameSize = value;
    applySettings(QStringList("frameSize"));
}

void SSDVModGUI::on_convolutional_toggled(bool checked)
{
    m_settings.m_convolutional = checked;
    applySettings(QStringList("convolutional"));
}

void SSDVModGUI::on_differential_toggled(bool checked)
{
    m_settings.m_differential = checked;
    applySettings(QStringList("differential"));
}

void SSDVModGUI::on_randomizer_toggled(bool checked)
{
    m_settings.m_randomizer = checked;
    applySettings(QStringList("randomizer"));
}

void SSDVModGUI::on_rs_toggled(bool checked)
{
    m_settings.m_rs = checked;
    ui->dualBasis->setEnabled(checked);
    applySettings(QStringList("rs"));
}

void SSDVModGUI::on_dualBasis_toggled(bool checked)
{
    m_settings.m_dualBasis = checked;
    applySettings(QStringList("dualBasis"));
}

void SSDVModGUI::on_imageFilename_editingFinished()
{
    m_settings.m_imageFilename = ui->imageFilename->text().trimmed();
    applySettings(QStringList("imageFilename"));
    showPreview();
}

void SSDVModGUI::on_imageFileBrowse_clicked()
{
    QStringList filters;

    for (const auto& format : QImageReader::supportedImageFormats()) {
        filters.append("*." + QString(format));
    }

    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Select image to send"),
        QFileInfo(m_settings.m_imageFilename).path(),
        tr("Images (%1)").arg(filters.join(" ")));

    if (!fileName.isEmpty())
    {
        m_settings.m_imageFilename = fileName;
        ui->imageFilename->setText(fileName);
        applySettings(QStringList("imageFilename"));
        showPreview();
    }
}

void SSDVModGUI::on_imageDirectoryBrowse_clicked()
{
    QString directory = QFileDialog::getExistingDirectory(
        this,
        tr("Select folder of images to send"),
        m_settings.m_imageFilename,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!directory.isEmpty())
    {
        m_settings.m_imageFilename = directory;
        ui->imageFilename->setText(directory);
        applySettings(QStringList("imageFilename"));
        showPreview();
    }
}

void SSDVModGUI::on_imageId_valueChanged(int value)
{
    m_settings.m_imageId = value;
    applySettings(QStringList("imageId"));
}

void SSDVModGUI::on_quality_valueChanged(int value)
{
    m_settings.m_quality = value;
    applySettings(QStringList("quality"));
}

void SSDVModGUI::on_width_valueChanged(int value)
{
    m_settings.m_width = value & ~15;
    applySettings(QStringList("width"));
    showPreview();
}

void SSDVModGUI::on_height_valueChanged(int value)
{
    m_settings.m_height = value & ~15;
    applySettings(QStringList("height"));
    showPreview();
}

void SSDVModGUI::on_fill_toggled(bool checked)
{
    m_settings.m_fill = checked;
    applySettings(QStringList("fill"));
    showPreview();
}

void SSDVModGUI::on_preambleMs_valueChanged(int value)
{
    m_settings.m_preambleMs = value;
    applySettings(QStringList("preambleMs"));
}

void SSDVModGUI::on_repeat_toggled(bool checked)
{
    m_settings.m_repeat = checked;
    applySettings(QStringList("repeat"));
}

void SSDVModGUI::on_repeatDelay_valueChanged(int value)
{
    m_settings.m_repeatDelay = value;
    applySettings(QStringList("repeatDelay"));
}

void SSDVModGUI::on_tx_clicked()
{
    m_ssdvMod->getInputMessageQueue()->push(SSDVMod::MsgTx::create());
}

void SSDVModGUI::on_stop_clicked()
{
    m_ssdvMod->getInputMessageQueue()->push(SSDVMod::MsgStop::create());
}

void SSDVModGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;

    getRollupContents()->saveState(m_rollupState);
    applySettings(QStringList("rollupState"));
}

void SSDVModGUI::onMenuDialogCalled(const QPoint &p)
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
            dialog.setNumberOfStreams(m_ssdvMod->getNumberOfDeviceStreams());
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

SSDVModGUI::SSDVModGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSource *channelTx, QWidget* parent) :
    ChannelGUI(parent),
    ui(new Ui::SSDVModGUI),
    m_pluginAPI(pluginAPI),
    m_deviceUISet(deviceUISet),
    m_channelMarker(this),
    m_deviceCenterFrequency(0),
    m_basebandSampleRate(1),
    m_doApplySettings(true),
    m_tickCount(0)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/channeltx/modssdv/readme.md";
    RollupContents *rollupContents = getRollupContents();
    ui->setupUi(rollupContents);
    setSizePolicy(rollupContents->sizePolicy());
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));

    m_ssdvMod = (SSDVMod*) channelTx;
    m_ssdvMod->setMessageQueueToGUI(getInputMessageQueue());

    connect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick()));

    ui->deltaFrequencyLabel->setText(QString("%1f").arg(QChar(0x94, 0x03)));
    ui->deltaFrequency->setColorMapper(ColorMapper(ColorMapper::GrayGold));
    ui->deltaFrequency->setValueRange(false, 7, -9999999, 9999999);
    ui->image->installEventFilter(this);

    m_channelMarker.blockSignals(true);
    m_channelMarker.setColor(QColor::fromRgb(m_settings.m_rgbColor));
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setTitle("SSDV Modulator");
    m_channelMarker.setSourceOrSinkStream(false);
    updateBandwidth();
    m_channelMarker.blockSignals(false);
    m_channelMarker.setVisible(true); // activate signal on the last setting only

    m_deviceUISet->addChannelMarker(&m_channelMarker);

    connect(&m_channelMarker, SIGNAL(changedByCursor()), this, SLOT(channelMarkerChangedByCursor()));
    connect(&m_channelMarker, SIGNAL(highlightedByCursor()), this, SLOT(channelMarkerHighlightedByCursor()));
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));
    m_ssdvMod->setLevelMeter(ui->volumeMeter);

    m_settings.setChannelMarker(&m_channelMarker);
    m_settings.setRollupState(&m_rollupState);

    displaySettings();
    makeUIConnections();
    applySettings(QStringList(), true);
    showPreview();
    ui->tx->setEnabled(!m_ssdvMod->isTransmitting());
    DialPopup::addPopupsToChildDials(this);
    m_resizer.enableChildMouseTracking();
}

SSDVModGUI::~SSDVModGUI()
{
    delete ui;
}

void SSDVModGUI::applySettings(const QStringList& settingsKeys, bool force)
{
    if (m_doApplySettings)
    {
        SSDVMod::MsgConfigureSSDVMod *msg = SSDVMod::MsgConfigureSSDVMod::create(settingsKeys, m_settings, force);
        m_ssdvMod->getInputMessageQueue()->push(msg);
    }
}

void SSDVModGUI::displaySettings()
{
    m_channelMarker.blockSignals(true);
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setTitle(m_settings.m_title);
    updateBandwidth();
    m_channelMarker.blockSignals(false);
    m_channelMarker.setColor(m_settings.m_rgbColor); // activate signal on the last setting only

    setTitleColor(m_settings.m_rgbColor);
    setWindowTitle(m_channelMarker.getTitle());
    setTitle(m_channelMarker.getTitle());
    updateIndexLabel();

    blockSignals(true);
    bool doApplySettings = m_doApplySettings;
    m_doApplySettings = false;

    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    ui->channelMute->setChecked(m_settings.m_channelMute);
    ui->gain->setValue(std::round(m_settings.m_gain));
    int baudIndex = ui->baudRate->findText(QString::number(m_settings.m_baudRate));

    if (baudIndex < 0)
    {
        ui->baudRate->addItem(QString::number(m_settings.m_baudRate));
        baudIndex = ui->baudRate->count() - 1;
    }

    ui->baudRate->setCurrentIndex(baudIndex);
    ui->rrcRolloff->setValue(std::round(m_settings.m_rrcRolloff * 100.0f));
    ui->callsign->setText(m_settings.m_callsign);
    ui->spacecraftId->setValue(m_settings.m_spacecraftId);
    ui->virtualChannel->setValue(m_settings.m_virtualChannel);
    ui->packetFormat->setCurrentIndex(m_settings.m_packetFormat);
    ui->frameSize->setValue(m_settings.m_frameSize);
    ui->convolutional->setChecked(m_settings.m_convolutional);
    ui->differential->setChecked(m_settings.m_differential);
    ui->randomizer->setChecked(m_settings.m_randomizer);
    ui->rs->setChecked(m_settings.m_rs);
    ui->dualBasis->setChecked(m_settings.m_dualBasis);
    ui->dualBasis->setEnabled(m_settings.m_rs);
    ui->imageFilename->setText(m_settings.m_imageFilename);
    ui->imageId->setValue(m_settings.m_imageId);
    ui->quality->setValue(m_settings.m_quality);
    ui->width->setValue(m_settings.m_width);
    ui->height->setValue(m_settings.m_height);
    ui->fill->setChecked(m_settings.m_fill);
    ui->preambleMs->setValue(m_settings.m_preambleMs);
    ui->repeat->setChecked(m_settings.m_repeat);
    ui->repeatDelay->setValue(std::round(m_settings.m_repeatDelay));

    getRollupContents()->restoreState(m_rollupState);
    updateAbsoluteCenterFrequency();

    m_doApplySettings = doApplySettings;
    blockSignals(false);
}

void SSDVModGUI::tick()
{
    double powDb = CalcDb::dbPower(m_ssdvMod->getMagSq());

    if (m_tickCount % 4 == 0) {
        ui->channelPower->setText(QString::number(powDb, 'f', 1));
    }

    m_tickCount++;
}

void SSDVModGUI::makeUIConnections()
{
    QObject::connect(ui->deltaFrequency, &ValueDialZ::changed, this, &SSDVModGUI::on_deltaFrequency_changed);
    QObject::connect(ui->channelMute, &QToolButton::toggled, this, &SSDVModGUI::on_channelMute_toggled);
    QObject::connect(ui->gain, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVModGUI::on_gain_valueChanged);
    QObject::connect(ui->baudRate, qOverload<int>(&QComboBox::currentIndexChanged), this, &SSDVModGUI::on_baudRate_currentIndexChanged);
    QObject::connect(ui->rrcRolloff, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVModGUI::on_rrcRolloff_valueChanged);
    QObject::connect(ui->callsign, &QLineEdit::editingFinished, this, &SSDVModGUI::on_callsign_editingFinished);
    QObject::connect(ui->spacecraftId, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVModGUI::on_spacecraftId_valueChanged);
    QObject::connect(ui->virtualChannel, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVModGUI::on_virtualChannel_valueChanged);
    QObject::connect(ui->packetFormat, qOverload<int>(&QComboBox::currentIndexChanged), this, &SSDVModGUI::on_packetFormat_currentIndexChanged);
    QObject::connect(ui->frameSize, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVModGUI::on_frameSize_valueChanged);
    QObject::connect(ui->convolutional, &ButtonSwitch::toggled, this, &SSDVModGUI::on_convolutional_toggled);
    QObject::connect(ui->differential, &ButtonSwitch::toggled, this, &SSDVModGUI::on_differential_toggled);
    QObject::connect(ui->randomizer, &ButtonSwitch::toggled, this, &SSDVModGUI::on_randomizer_toggled);
    QObject::connect(ui->rs, &ButtonSwitch::toggled, this, &SSDVModGUI::on_rs_toggled);
    QObject::connect(ui->dualBasis, &ButtonSwitch::toggled, this, &SSDVModGUI::on_dualBasis_toggled);
    QObject::connect(ui->imageFilename, &QLineEdit::editingFinished, this, &SSDVModGUI::on_imageFilename_editingFinished);
    QObject::connect(ui->imageFileBrowse, &QToolButton::clicked, this, &SSDVModGUI::on_imageFileBrowse_clicked);
    QObject::connect(ui->imageDirectoryBrowse, &QToolButton::clicked, this, &SSDVModGUI::on_imageDirectoryBrowse_clicked);
    QObject::connect(ui->imageId, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVModGUI::on_imageId_valueChanged);
    QObject::connect(ui->quality, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVModGUI::on_quality_valueChanged);
    QObject::connect(ui->width, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVModGUI::on_width_valueChanged);
    QObject::connect(ui->height, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVModGUI::on_height_valueChanged);
    QObject::connect(ui->fill, &ButtonSwitch::toggled, this, &SSDVModGUI::on_fill_toggled);
    QObject::connect(ui->preambleMs, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVModGUI::on_preambleMs_valueChanged);
    QObject::connect(ui->repeat, &ButtonSwitch::toggled, this, &SSDVModGUI::on_repeat_toggled);
    QObject::connect(ui->repeatDelay, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVModGUI::on_repeatDelay_valueChanged);
    QObject::connect(ui->tx, &QPushButton::clicked, this, &SSDVModGUI::on_tx_clicked);
    QObject::connect(ui->stop, &QPushButton::clicked, this, &SSDVModGUI::on_stop_clicked);
}

void SSDVModGUI::updateAbsoluteCenterFrequency()
{
    setStatusFrequency(m_deviceCenterFrequency + m_settings.m_inputFrequencyOffset);
}
