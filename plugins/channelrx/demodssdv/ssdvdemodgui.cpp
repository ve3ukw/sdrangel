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
#include <QPixmap>
#include <QScrollBar>
#include <QTableWidgetItem>

#include "device/deviceuiset.h"
#include "dsp/dspcommands.h"
#include "dsp/glscopesettings.h"
#include "dsp/scopevis.h"
#include "gui/basicchannelsettingsdialog.h"
#include "gui/dialpopup.h"
#include "gui/dialogpositioner.h"
#include "plugin/pluginapi.h"
#include "util/db.h"
#include "maincore.h"

#include "ui_ssdvdemodgui.h"
#include "ssdvdemodreport.h"
#include "ssdvdemod.h"
#include "ssdvdemodgui.h"

// Links selected with the preset combo box, after "Custom"
struct SSDVDemodPreset
{
    int m_modulation;
    int m_baudRate;
    int m_fskDeviation;
    int m_framing;
    int m_packetFormat;
};

static const SSDVDemodPreset m_presets[] = {
    {SSDVDemodSettings::BPSK, 9600, 0, SSDVDemodSettings::CCSDS, SSDVDemodSettings::AO123},          // AO-123
    {SSDVDemodSettings::FSK, 9600, 2400, SSDVDemodSettings::USP, SSDVDemodSettings::Standard},       // Luca 9k6
    {SSDVDemodSettings::FSK, 2400, 600, SSDVDemodSettings::USP, SSDVDemodSettings::Standard},        // Luca 2k4
    {SSDVDemodSettings::FSK, 800, 800, SSDVDemodSettings::Genesis, SSDVDemodSettings::Standard}      // HADES-SA
};

SSDVDemodGUI* SSDVDemodGUI::create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel)
{
    SSDVDemodGUI* gui = new SSDVDemodGUI(pluginAPI, deviceUISet, rxChannel);
    return gui;
}

void SSDVDemodGUI::destroy()
{
    delete this;
}

void SSDVDemodGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applySettings(QStringList(), true);
}

QByteArray SSDVDemodGUI::serialize() const
{
    return m_settings.serialize();
}

bool SSDVDemodGUI::deserialize(const QByteArray& data)
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

bool SSDVDemodGUI::handleMessage(const Message& message)
{
    if (SSDVDemod::MsgConfigureSSDVDemod::match(message))
    {
        qDebug("SSDVDemodGUI::handleMessage: SSDVDemod::MsgConfigureSSDVDemod");
        const SSDVDemod::MsgConfigureSSDVDemod& cfg = (const SSDVDemod::MsgConfigureSSDVDemod&) message;

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
    else if (SSDVDemodReport::MsgFrame::match(message))
    {
        // Counts are shown by tick()
        return true;
    }
    else if (SSDVDemodReport::MsgPacket::match(message))
    {
        const SSDVDemodReport::MsgPacket& report = (const SSDVDemodReport::MsgPacket&) message;
        const SSDV::PacketInfo& info = report.getInfo();
        bool scrollToBottom = ui->packets->verticalScrollBar()->value() == ui->packets->verticalScrollBar()->maximum();

        if (ui->packets->rowCount() >= m_maxPacketRows) {
            ui->packets->removeRow(0);
        }

        int row = ui->packets->rowCount();
        ui->packets->setSortingEnabled(false);
        ui->packets->setRowCount(row + 1);
        ui->packets->setItem(row, PACKET_COL_DATE, new QTableWidgetItem(report.getDateTime().date().toString()));
        ui->packets->setItem(row, PACKET_COL_TIME, new QTableWidgetItem(report.getDateTime().time().toString()));
        ui->packets->setItem(row, PACKET_COL_CALLSIGN, new QTableWidgetItem(info.m_callsign));
        ui->packets->setItem(row, PACKET_COL_IMAGE, new QTableWidgetItem(QString::number(info.m_imageId)));
        ui->packets->setItem(row, PACKET_COL_PACKET, new QTableWidgetItem(QString::number(info.m_packetId)));
        ui->packets->setItem(row, PACKET_COL_SIZE, new QTableWidgetItem(QString("%1x%2").arg(info.m_width).arg(info.m_height)));
        ui->packets->setItem(row, PACKET_COL_MCU, new QTableWidgetItem(info.m_mcuId == 0xFFFF ? "-" : QString::number(info.m_mcuId)));
        ui->packets->setItem(row, PACKET_COL_EOI, new QTableWidgetItem(info.m_eoi ? "Y" : ""));
        ui->packets->setItem(row, PACKET_COL_RS, new QTableWidgetItem(info.m_fec ? QString::number(info.m_rsErrors) : "-"));
        ui->packets->setItem(row, PACKET_COL_NOTE, new QTableWidgetItem(report.getRepeat() ? tr("Repeat") : ""));

        if (scrollToBottom) {
            ui->packets->scrollToBottom();
        }

        return true;
    }
    else if (SSDVDemodReport::MsgImage::match(message))
    {
        const SSDVDemodReport::MsgImage& report = (const SSDVDemodReport::MsgImage&) message;
        m_image = report.getImage();
        QString count = report.getPacketCount() > 0 ? QString::number(report.getPacketCount()) : "?";
        QString status = tr("%1 #%2: %3/%4 packets, %5 missing")
            .arg(report.getCallsign())
            .arg(report.getImageId())
            .arg(report.getPacketsReceived())
            .arg(count)
            .arg(report.getMissingPackets());

        if (report.getComplete()) {
            status += tr(", complete");
        } else {
            status += QString(", %1%").arg(std::round(report.getProgress() * 100.0f));
        }

        if (!report.getSavedFile().isEmpty()) {
            ui->status->setToolTip(tr("Saved to %1").arg(report.getSavedFile()));
        }

        setStatus(status);
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

void SSDVDemodGUI::handleInputMessages()
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

void SSDVDemodGUI::setStatus(const QString& status)
{
    ui->status->setText(status);
    setStatusText(status);
}

void SSDVDemodGUI::updateImage()
{
    if (m_image.isNull())
    {
        ui->image->clear();
        return;
    }

    ui->image->setPixmap(QPixmap::fromImage(m_image).scaled(
        ui->image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void SSDVDemodGUI::updateBandwidth()
{
    if (m_settings.m_modulation == SSDVDemodSettings::FSK) {
        m_channelMarker.setBandwidth(2 * m_settings.m_fskDeviation + m_settings.m_baudRate);
    } else {
        m_channelMarker.setBandwidth(m_settings.m_baudRate * (1.0f + m_settings.m_rrcRolloff));
    }
}

// Only show the settings used by the modulation and framing as enabled
void SSDVDemodGUI::updateEnabled()
{
    bool fsk = m_settings.m_modulation == SSDVDemodSettings::FSK;
    bool ccsds = m_settings.m_framing == SSDVDemodSettings::CCSDS;

    ui->fskDeviation->setEnabled(fsk);
    ui->rrcRolloff->setEnabled(!fsk);
    ui->pllBandwidth->setEnabled(!fsk);
    ui->packetFormat->setEnabled(m_settings.m_framing != SSDVDemodSettings::Genesis);
    ui->frameSize->setEnabled(ccsds);
    ui->convolutional->setEnabled(ccsds);
    ui->differential->setEnabled(ccsds);
    ui->randomizer->setEnabled(ccsds);
    ui->rs->setEnabled(ccsds);
    ui->dualBasis->setEnabled(ccsds && m_settings.m_rs);
}

// Preset the settings match, or 0 (Custom)
int SSDVDemodGUI::findPreset() const
{
    for (int i = 0; i < (int) (sizeof(m_presets) / sizeof(m_presets[0])); i++)
    {
        const SSDVDemodPreset& preset = m_presets[i];

        if ((m_settings.m_modulation != preset.m_modulation)
            || (m_settings.m_baudRate != preset.m_baudRate)
            || (m_settings.m_framing != preset.m_framing)) {
            continue;
        }
        if ((preset.m_modulation == SSDVDemodSettings::FSK) && (m_settings.m_fskDeviation != preset.m_fskDeviation)) {
            continue;
        }
        if ((preset.m_framing != SSDVDemodSettings::Genesis) && (m_settings.m_packetFormat != preset.m_packetFormat)) {
            continue;
        }
        if ((preset.m_framing == SSDVDemodSettings::CCSDS)
            && ((m_settings.m_frameSize != 223) || !m_settings.m_convolutional || !m_settings.m_differential
                || !m_settings.m_randomizer || !m_settings.m_rs || m_settings.m_dualBasis)) {
            continue;
        }

        return i + 1;
    }

    return 0;
}

void SSDVDemodGUI::updatePreset()
{
    ui->preset->blockSignals(true);
    ui->preset->setCurrentIndex(findPreset());
    ui->preset->blockSignals(false);
}

bool SSDVDemodGUI::eventFilter(QObject *obj, QEvent *event)
{
    if ((obj == ui->image) && (event->type() == QEvent::Resize)) {
        updateImage();
    }

    return ChannelGUI::eventFilter(obj, event);
}

void SSDVDemodGUI::resizeTable()
{
    // Fill a row with typical values to size the columns
    int row = ui->packets->rowCount();
    ui->packets->setRowCount(row + 1);
    ui->packets->setItem(row, PACKET_COL_DATE, new QTableWidgetItem("Mon Nov 30 2026"));
    ui->packets->setItem(row, PACKET_COL_TIME, new QTableWidgetItem("12:00:00"));
    ui->packets->setItem(row, PACKET_COL_CALLSIGN, new QTableWidgetItem("VE3UKW"));
    ui->packets->setItem(row, PACKET_COL_IMAGE, new QTableWidgetItem("255"));
    ui->packets->setItem(row, PACKET_COL_PACKET, new QTableWidgetItem("1000"));
    ui->packets->setItem(row, PACKET_COL_SIZE, new QTableWidgetItem("4080x4080"));
    ui->packets->setItem(row, PACKET_COL_MCU, new QTableWidgetItem("10000"));
    ui->packets->setItem(row, PACKET_COL_EOI, new QTableWidgetItem("EOI"));
    ui->packets->setItem(row, PACKET_COL_RS, new QTableWidgetItem("RS"));
    ui->packets->setItem(row, PACKET_COL_NOTE, new QTableWidgetItem("Repeat"));
    ui->packets->resizeColumnsToContents();
    ui->packets->removeRow(row);
}

void SSDVDemodGUI::channelMarkerChangedByCursor()
{
    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void SSDVDemodGUI::channelMarkerHighlightedByCursor()
{
    setHighlighted(m_channelMarker.getHighlighted());
}

void SSDVDemodGUI::on_deltaFrequency_changed(qint64 value)
{
    m_channelMarker.setCenterFrequency(value);
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    updateAbsoluteCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void SSDVDemodGUI::on_preset_currentIndexChanged(int index)
{
    if (!m_doApplySettings || (index <= 0)) {
        return;
    }

    const SSDVDemodPreset& preset = m_presets[index - 1];
    QStringList keys = {"modulation", "baudRate", "framing"};
    m_settings.m_modulation = preset.m_modulation;
    m_settings.m_baudRate = preset.m_baudRate;
    m_settings.m_framing = preset.m_framing;

    if (preset.m_modulation == SSDVDemodSettings::FSK)
    {
        m_settings.m_fskDeviation = preset.m_fskDeviation;
        keys.append("fskDeviation");
    }

    if (preset.m_framing != SSDVDemodSettings::Genesis)
    {
        m_settings.m_packetFormat = preset.m_packetFormat;
        keys.append("packetFormat");
    }

    if (preset.m_framing == SSDVDemodSettings::CCSDS)
    {
        // The coding AO-123 uses
        m_settings.m_frameSize = 223;
        m_settings.m_convolutional = true;
        m_settings.m_differential = true;
        m_settings.m_randomizer = true;
        m_settings.m_rs = true;
        m_settings.m_dualBasis = false;
        keys.append({"frameSize", "convolutional", "differential", "randomizer", "rs", "dualBasis"});
    }

    displaySettings();
    applySettings(keys);
}

void SSDVDemodGUI::on_modulation_currentIndexChanged(int index)
{
    m_settings.m_modulation = index;
    updateBandwidth();
    updateEnabled();
    applySettings(QStringList("modulation"));
}

void SSDVDemodGUI::on_framing_currentIndexChanged(int index)
{
    m_settings.m_framing = index;
    updateEnabled();
    applySettings(QStringList("framing"));
}

void SSDVDemodGUI::on_baudRate_currentIndexChanged(int index)
{
    m_settings.m_baudRate = ui->baudRate->itemText(index).toInt();
    updateBandwidth();
    applySettings(QStringList("baudRate"));
}

void SSDVDemodGUI::on_fskDeviation_valueChanged(int value)
{
    m_settings.m_fskDeviation = value;
    updateBandwidth();
    applySettings(QStringList("fskDeviation"));
}

void SSDVDemodGUI::on_rrcRolloff_valueChanged(int value)
{
    m_settings.m_rrcRolloff = value / 100.0f;
    updateBandwidth();
    applySettings(QStringList("rrcRolloff"));
}

void SSDVDemodGUI::on_pllBandwidth_valueChanged(int value)
{
    m_settings.m_pllBandwidth = value / 1000.0f;
    applySettings(QStringList("pllBandwidth"));
}

void SSDVDemodGUI::on_afc_toggled(bool checked)
{
    m_settings.m_afc = checked;
    applySettings(QStringList("afc"));
}

void SSDVDemodGUI::on_packetFormat_currentIndexChanged(int index)
{
    m_settings.m_packetFormat = index;
    applySettings(QStringList("packetFormat"));
}

void SSDVDemodGUI::on_frameSize_valueChanged(int value)
{
    m_settings.m_frameSize = value;
    applySettings(QStringList("frameSize"));
}

void SSDVDemodGUI::on_convolutional_toggled(bool checked)
{
    m_settings.m_convolutional = checked;
    applySettings(QStringList("convolutional"));
}

void SSDVDemodGUI::on_differential_toggled(bool checked)
{
    m_settings.m_differential = checked;
    applySettings(QStringList("differential"));
}

void SSDVDemodGUI::on_randomizer_toggled(bool checked)
{
    m_settings.m_randomizer = checked;
    applySettings(QStringList("randomizer"));
}

void SSDVDemodGUI::on_rs_toggled(bool checked)
{
    m_settings.m_rs = checked;
    updateEnabled();
    applySettings(QStringList("rs"));
}

void SSDVDemodGUI::on_dualBasis_toggled(bool checked)
{
    m_settings.m_dualBasis = checked;
    applySettings(QStringList("dualBasis"));
}

void SSDVDemodGUI::on_autoSave_toggled(bool checked)
{
    m_settings.m_autoSave = checked;
    applySettings(QStringList("autoSave"));
}

void SSDVDemodGUI::on_autoSavePath_editingFinished()
{
    m_settings.m_autoSavePath = ui->autoSavePath->text().trimmed();
    applySettings(QStringList("autoSavePath"));
}

void SSDVDemodGUI::on_autoSavePathBrowse_clicked()
{
    QString directory = QFileDialog::getExistingDirectory(
        this,
        tr("Select folder for SSDV images"),
        m_settings.m_autoSavePath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!directory.isEmpty())
    {
        m_settings.m_autoSavePath = directory;
        ui->autoSavePath->setText(directory);
        applySettings(QStringList("autoSavePath"));
    }
}

void SSDVDemodGUI::on_saveFrames_toggled(bool checked)
{
    m_settings.m_saveFrames = checked;
    applySettings(QStringList("saveFrames"));
}

void SSDVDemodGUI::on_framesFilename_editingFinished()
{
    m_settings.m_framesFilename = ui->framesFilename->text().trimmed();
    applySettings(QStringList("framesFilename"));
}

void SSDVDemodGUI::on_framesFilenameBrowse_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Select file to append frames to"),
        m_settings.m_framesFilename,
        tr("KISS files (*.kss);;All files (*)"),
        nullptr,
        QFileDialog::DontConfirmOverwrite);

    if (!fileName.isEmpty())
    {
        m_settings.m_framesFilename = fileName;
        ui->framesFilename->setText(fileName);
        applySettings(QStringList("framesFilename"));
    }
}

void SSDVDemodGUI::on_saveImage_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Save SSDV image"),
        m_settings.m_autoSavePath,
        tr("JPEG images (*.jpg);;PNG images (*.png)"));

    if (!fileName.isEmpty() && !m_ssdvDemod->saveImage(fileName)) {
        setStatus(tr("Could not save %1").arg(fileName));
    }
}

void SSDVDemodGUI::on_clearImage_clicked()
{
    m_ssdvDemod->getInputMessageQueue()->push(SSDVDemod::MsgClear::create());
    m_image = QImage();
    setStatus(tr("Waiting"));
    ui->status->setToolTip(tr("Image being received"));
    updateImage();
}

void SSDVDemodGUI::on_clearPackets_clicked()
{
    ui->packets->setRowCount(0);
}

void SSDVDemodGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;

    getRollupContents()->saveState(m_rollupState);
    applySettings(QStringList("rollupState"));
}

void SSDVDemodGUI::onMenuDialogCalled(const QPoint &p)
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
            dialog.setNumberOfStreams(m_ssdvDemod->getNumberOfDeviceStreams());
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

SSDVDemodGUI::SSDVDemodGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel, QWidget* parent) :
    ChannelGUI(parent),
    ui(new Ui::SSDVDemodGUI),
    m_pluginAPI(pluginAPI),
    m_deviceUISet(deviceUISet),
    m_channelMarker(this),
    m_deviceCenterFrequency(0),
    m_basebandSampleRate(1),
    m_doApplySettings(true),
    m_tickCount(0)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/channelrx/demodssdv/readme.md";
    RollupContents *rollupContents = getRollupContents();
    ui->setupUi(rollupContents);
    setSizePolicy(rollupContents->sizePolicy());
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));

    m_ssdvDemod = reinterpret_cast<SSDVDemod*>(rxChannel);
    m_ssdvDemod->setMessageQueueToGUI(getInputMessageQueue());

    connect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick()));

    ui->deltaFrequencyLabel->setText(QString("%1f").arg(QChar(0x94, 0x03)));
    ui->deltaFrequency->setColorMapper(ColorMapper(ColorMapper::GrayGold));
    ui->deltaFrequency->setValueRange(false, 7, -9999999, 9999999);
    ui->channelPowerMeter->setColorTheme(LevelMeterSignalDB::ColorGreenAndBlue);
    ui->image->installEventFilter(this);

    // Constellation of the symbols
    m_scopeVis = m_ssdvDemod->getScopeSink();
    m_scopeVis->setGLScope(ui->glScope);
    ui->glScope->connectTimer(MainCore::instance()->getMasterTimer());
    ui->scopeGUI->setBuddies(m_scopeVis->getInputMessageQueue(), m_scopeVis, ui->glScope);
    ui->scopeGUI->setPreTrigger(1);
    GLScopeSettings::TraceData traceDataI, traceDataQ;
    traceDataI.m_projectionType = Projector::ProjectionReal;
    traceDataI.m_amp = 1.0;
    traceDataI.m_ofs = 0.0;
    traceDataQ.m_projectionType = Projector::ProjectionImag;
    traceDataQ.m_amp = 1.0;
    traceDataQ.m_ofs = 0.0;
    ui->scopeGUI->changeTrace(0, traceDataI);
    ui->scopeGUI->addTrace(traceDataQ);
    ui->scopeGUI->setDisplayMode(GLScopeSettings::DisplayXYV);
    ui->scopeGUI->focusOnTrace(0); // re-focus to take changes into account in the GUI
    m_scopeVis->setLiveRate(m_settings.m_baudRate);
    m_scopeVis->configure(500, 1, 0, 0, true);

    m_channelMarker.blockSignals(true);
    m_channelMarker.setColor(QColor::fromRgb(m_settings.m_rgbColor));
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setTitle("SSDV Demodulator");
    updateBandwidth();
    m_channelMarker.blockSignals(false);
    m_channelMarker.setVisible(true); // activate signal on the last setting only

    m_settings.setChannelMarker(&m_channelMarker);
    m_settings.setRollupState(&m_rollupState);
    m_settings.setScopeGUI(ui->scopeGUI);

    m_deviceUISet->addChannelMarker(&m_channelMarker);

    connect(&m_channelMarker, SIGNAL(changedByCursor()), this, SLOT(channelMarkerChangedByCursor()));
    connect(&m_channelMarker, SIGNAL(highlightedByCursor()), this, SLOT(channelMarkerHighlightedByCursor()));
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));

    resizeTable();
    setStatus(tr("Waiting"));

    displaySettings();
    makeUIConnections();
    applySettings(QStringList(), true);
    DialPopup::addPopupsToChildDials(this);
    m_resizer.enableChildMouseTracking();
}

SSDVDemodGUI::~SSDVDemodGUI()
{
    delete ui;
}

void SSDVDemodGUI::applySettings(const QStringList& settingsKeys, bool force)
{
    if (m_doApplySettings)
    {
        SSDVDemod::MsgConfigureSSDVDemod* message = SSDVDemod::MsgConfigureSSDVDemod::create(settingsKeys, m_settings, force);
        m_ssdvDemod->getInputMessageQueue()->push(message);
        updatePreset();
    }
}

void SSDVDemodGUI::displaySettings()
{
    m_channelMarker.blockSignals(true);
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    updateBandwidth();
    m_channelMarker.setTitle(m_settings.m_title);
    m_channelMarker.blockSignals(false);
    m_channelMarker.setColor(m_settings.m_rgbColor); // activate signal on the last setting only

    setTitleColor(m_settings.m_rgbColor);
    setWindowTitle(m_channelMarker.getTitle());
    setTitle(m_channelMarker.getTitle());

    blockSignals(true);
    bool doApplySettings = m_doApplySettings;
    m_doApplySettings = false;

    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    int baudIndex = ui->baudRate->findText(QString::number(m_settings.m_baudRate));

    if (baudIndex < 0)
    {
        ui->baudRate->addItem(QString::number(m_settings.m_baudRate));
        baudIndex = ui->baudRate->count() - 1;
    }

    ui->baudRate->setCurrentIndex(baudIndex);
    ui->preset->setCurrentIndex(findPreset());
    ui->modulation->setCurrentIndex(m_settings.m_modulation);
    ui->framing->setCurrentIndex(m_settings.m_framing);
    ui->fskDeviation->setValue(m_settings.m_fskDeviation);
    ui->rrcRolloff->setValue(std::round(m_settings.m_rrcRolloff * 100.0f));
    ui->pllBandwidth->setValue(std::round(m_settings.m_pllBandwidth * 1000.0f));
    ui->afc->setChecked(m_settings.m_afc);
    ui->packetFormat->setCurrentIndex(m_settings.m_packetFormat);
    ui->frameSize->setValue(m_settings.m_frameSize);
    ui->convolutional->setChecked(m_settings.m_convolutional);
    ui->differential->setChecked(m_settings.m_differential);
    ui->randomizer->setChecked(m_settings.m_randomizer);
    ui->rs->setChecked(m_settings.m_rs);
    ui->dualBasis->setChecked(m_settings.m_dualBasis);
    updateEnabled();
    ui->autoSave->setChecked(m_settings.m_autoSave);
    ui->autoSavePath->setText(m_settings.m_autoSavePath);
    ui->saveFrames->setChecked(m_settings.m_saveFrames);
    ui->framesFilename->setText(m_settings.m_framesFilename);

    updateIndexLabel();
    getRollupContents()->restoreState(m_rollupState);
    updateAbsoluteCenterFrequency();

    m_doApplySettings = doApplySettings;
    blockSignals(false);
}

void SSDVDemodGUI::tick()
{
    double magsqAvg, magsqPeak;
    int nbMagsqSamples;
    m_ssdvDemod->getMagSqLevels(magsqAvg, magsqPeak, nbMagsqSamples);
    double powDbAvg = CalcDb::dbPower(magsqAvg);
    double powDbPeak = CalcDb::dbPower(magsqPeak);

    ui->channelPowerMeter->levelChanged(
            (100.0f + powDbAvg) / 100.0f,
            (100.0f + powDbPeak) / 100.0f,
            nbMagsqSamples);

    if (m_tickCount % 4 == 0)
    {
        ui->channelPower->setText(QString::number(powDbAvg, 'f', 1));

        bool locked = m_ssdvDemod->isLocked();
        ui->lock->setText(locked ? tr("Locked") : tr("Unlocked"));
        ui->lock->setStyleSheet(locked ? "QLabel { color: lime; }" : "");
        ui->frequencyOffset->setText(QString("%1 Hz").arg(m_ssdvDemod->getFrequencyOffset(), 0, 'f', 0));
        ui->evm->setText(QString("%1%").arg(std::sqrt(m_ssdvDemod->getEvm()) * 100.0f, 0, 'f', 0));
        ui->frames->setText(QString("%1/%2").arg(m_ssdvDemod->getFramesReceived()).arg(m_ssdvDemod->getFramesBad()));
        ui->packetCount->setText(QString::number(m_ssdvDemod->getPacketsReceived()));
    }

    m_tickCount++;
}

void SSDVDemodGUI::makeUIConnections()
{
    QObject::connect(ui->deltaFrequency, &ValueDialZ::changed, this, &SSDVDemodGUI::on_deltaFrequency_changed);
    QObject::connect(ui->preset, qOverload<int>(&QComboBox::currentIndexChanged), this, &SSDVDemodGUI::on_preset_currentIndexChanged);
    QObject::connect(ui->modulation, qOverload<int>(&QComboBox::currentIndexChanged), this, &SSDVDemodGUI::on_modulation_currentIndexChanged);
    QObject::connect(ui->framing, qOverload<int>(&QComboBox::currentIndexChanged), this, &SSDVDemodGUI::on_framing_currentIndexChanged);
    QObject::connect(ui->baudRate, qOverload<int>(&QComboBox::currentIndexChanged), this, &SSDVDemodGUI::on_baudRate_currentIndexChanged);
    QObject::connect(ui->fskDeviation, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVDemodGUI::on_fskDeviation_valueChanged);
    QObject::connect(ui->rrcRolloff, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVDemodGUI::on_rrcRolloff_valueChanged);
    QObject::connect(ui->pllBandwidth, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVDemodGUI::on_pllBandwidth_valueChanged);
    QObject::connect(ui->afc, &ButtonSwitch::toggled, this, &SSDVDemodGUI::on_afc_toggled);
    QObject::connect(ui->packetFormat, qOverload<int>(&QComboBox::currentIndexChanged), this, &SSDVDemodGUI::on_packetFormat_currentIndexChanged);
    QObject::connect(ui->frameSize, qOverload<int>(&QSpinBox::valueChanged), this, &SSDVDemodGUI::on_frameSize_valueChanged);
    QObject::connect(ui->convolutional, &ButtonSwitch::toggled, this, &SSDVDemodGUI::on_convolutional_toggled);
    QObject::connect(ui->differential, &ButtonSwitch::toggled, this, &SSDVDemodGUI::on_differential_toggled);
    QObject::connect(ui->randomizer, &ButtonSwitch::toggled, this, &SSDVDemodGUI::on_randomizer_toggled);
    QObject::connect(ui->rs, &ButtonSwitch::toggled, this, &SSDVDemodGUI::on_rs_toggled);
    QObject::connect(ui->dualBasis, &ButtonSwitch::toggled, this, &SSDVDemodGUI::on_dualBasis_toggled);
    QObject::connect(ui->autoSave, &ButtonSwitch::toggled, this, &SSDVDemodGUI::on_autoSave_toggled);
    QObject::connect(ui->autoSavePath, &QLineEdit::editingFinished, this, &SSDVDemodGUI::on_autoSavePath_editingFinished);
    QObject::connect(ui->autoSavePathBrowse, &QToolButton::clicked, this, &SSDVDemodGUI::on_autoSavePathBrowse_clicked);
    QObject::connect(ui->saveFrames, &ButtonSwitch::toggled, this, &SSDVDemodGUI::on_saveFrames_toggled);
    QObject::connect(ui->framesFilename, &QLineEdit::editingFinished, this, &SSDVDemodGUI::on_framesFilename_editingFinished);
    QObject::connect(ui->framesFilenameBrowse, &QToolButton::clicked, this, &SSDVDemodGUI::on_framesFilenameBrowse_clicked);
    QObject::connect(ui->saveImage, &QToolButton::clicked, this, &SSDVDemodGUI::on_saveImage_clicked);
    QObject::connect(ui->clearImage, &QToolButton::clicked, this, &SSDVDemodGUI::on_clearImage_clicked);
    QObject::connect(ui->clearPackets, &QToolButton::clicked, this, &SSDVDemodGUI::on_clearPackets_clicked);
}

void SSDVDemodGUI::updateAbsoluteCenterFrequency()
{
    setStatusFrequency(m_deviceCenterFrequency + m_settings.m_inputFrequencyOffset);
}
