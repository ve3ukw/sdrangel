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

#ifndef INCLUDE_SSDVMODGUI_H
#define INCLUDE_SSDVMODGUI_H

#include <QImage>

#include "channel/channelgui.h"
#include "dsp/channelmarker.h"
#include "settings/rollupstate.h"
#include "util/messagequeue.h"

#include "ssdvmodsettings.h"

class PluginAPI;
class DeviceUISet;
class BasebandSampleSource;
class SSDVMod;

namespace Ui {
    class SSDVModGUI;
}

class SSDVModGUI : public ChannelGUI {
    Q_OBJECT

public:
    static SSDVModGUI* create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSource *channelTx);
    virtual void destroy();

    void resetToDefaults();
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
    virtual MessageQueue *getInputMessageQueue() { return &m_inputMessageQueue; }
    virtual void setWorkspaceIndex(int index) { m_settings.m_workspaceIndex = index; };
    virtual int getWorkspaceIndex() const { return m_settings.m_workspaceIndex; };
    virtual void setGeometryBytes(const QByteArray& blob) { m_settings.m_geometryBytes = blob; };
    virtual QByteArray getGeometryBytes() const { return m_settings.m_geometryBytes; };
    virtual QString getTitle() const { return m_settings.m_title; };
    virtual QColor getTitleColor() const  { return m_settings.m_rgbColor; };
    virtual void zetHidden(bool hidden) { m_settings.m_hidden = hidden; }
    virtual bool getHidden() const { return m_settings.m_hidden; }
    virtual ChannelMarker& getChannelMarker() { return m_channelMarker; }
    virtual int getStreamIndex() const { return m_settings.m_streamIndex; }
    virtual void setStreamIndex(int streamIndex) { m_settings.m_streamIndex = streamIndex; }

public slots:
    void channelMarkerChangedByCursor();
    void channelMarkerHighlightedByCursor();

private:
    Ui::SSDVModGUI* ui;
    PluginAPI* m_pluginAPI;
    DeviceUISet* m_deviceUISet;
    ChannelMarker m_channelMarker;
    RollupState m_rollupState;
    SSDVModSettings m_settings;
    qint64 m_deviceCenterFrequency;
    int m_basebandSampleRate;
    bool m_doApplySettings;
    SSDVMod* m_ssdvMod;
    uint32_t m_tickCount;
    MessageQueue m_inputMessageQueue;
    QImage m_image;

    explicit SSDVModGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSource *channelTx, QWidget* parent = nullptr);
    virtual ~SSDVModGUI();

    void applySettings(const QStringList& settingsKeys, bool force = false);
    void displaySettings();
    bool handleMessage(const Message& message);
    void makeUIConnections();
    void updateAbsoluteCenterFrequency();
    void updateImage();
    void updateBandwidth();
    void setStatus(const QString& status);
    void showPreview();

    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void on_deltaFrequency_changed(qint64 value);
    void on_channelMute_toggled(bool checked);
    void on_gain_valueChanged(int value);
    void on_baudRate_currentIndexChanged(int index);
    void on_rrcRolloff_valueChanged(int value);
    void on_callsign_editingFinished();
    void on_spacecraftId_valueChanged(int value);
    void on_virtualChannel_valueChanged(int value);
    void on_packetFormat_currentIndexChanged(int index);
    void on_frameSize_valueChanged(int value);
    void on_convolutional_toggled(bool checked);
    void on_differential_toggled(bool checked);
    void on_randomizer_toggled(bool checked);
    void on_rs_toggled(bool checked);
    void on_dualBasis_toggled(bool checked);
    void on_imageFilename_editingFinished();
    void on_imageFileBrowse_clicked();
    void on_imageDirectoryBrowse_clicked();
    void on_imageId_valueChanged(int value);
    void on_quality_valueChanged(int value);
    void on_width_valueChanged(int value);
    void on_height_valueChanged(int value);
    void on_fill_toggled(bool checked);
    void on_preambleMs_valueChanged(int value);
    void on_repeat_toggled(bool checked);
    void on_repeatDelay_valueChanged(int value);
    void on_tx_clicked();
    void on_stop_clicked();
    void onWidgetRolled(QWidget* widget, bool rollDown);
    void onMenuDialogCalled(const QPoint& p);
    void handleInputMessages();
    void tick();
};

#endif // INCLUDE_SSDVMODGUI_H
