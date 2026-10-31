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

#ifndef INCLUDE_VARAMODGUI_H
#define INCLUDE_VARAMODGUI_H

#include "channel/channelgui.h"
#include "dsp/channelmarker.h"
#include "settings/rollupstate.h"
#include "util/messagequeue.h"

#include "varamodsettings.h"

class PluginAPI;
class DeviceUISet;
class BasebandSampleSource;
class VARAMod;

namespace Ui {
    class VARAModGUI;
}

class VARAModGUI : public ChannelGUI {
    Q_OBJECT

public:
    static VARAModGUI* create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSource *txChannel);
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
    Ui::VARAModGUI* ui;
    PluginAPI* m_pluginAPI;
    DeviceUISet* m_deviceUISet;
    ChannelMarker m_channelMarker;
    RollupState m_rollupState;
    VARAModSettings m_settings;
    qint64 m_deviceCenterFrequency;
    int m_basebandSampleRate;
    bool m_doApplySettings;
    VARAMod* m_varaMod;
    uint32_t m_tickCount;
    int m_engineMode;
    MessageQueue m_inputMessageQueue;

    explicit VARAModGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSource *txChannel, QWidget* parent = nullptr);
    virtual ~VARAModGUI();

    void applySettings(const QStringList& settingsKeys, bool force = false);
    void displaySettings();
    void displayStatus();
    void displayMarker();
    bool handleMessage(const Message& message);
    void makeUIConnections();
    void updateAbsoluteCenterFrequency();

private slots:
    void on_deltaFrequency_changed(qint64 value);
    void on_channelMute_toggled(bool checked);
    void on_cmdPort_editingFinished();
    void on_fmDeviation_valueChanged(int value);
    void on_gain_valueChanged(int value);
    void on_linkFrequencyOffset_toggled(bool checked);
    void on_pttEnabled_toggled(bool checked);
    void on_pttAddress_editingFinished();
    void on_pttPort_editingFinished();
    void on_pttQuery_clicked();
    void on_pttLeadMs_valueChanged(int value);
    void on_pttTailMs_valueChanged(int value);
    void on_txPathMs_valueChanged(int value);
    void onWidgetRolled(QWidget* widget, bool rollDown);
    void onMenuDialogCalled(const QPoint& p);
    void handleInputMessages();
    void tick();
};

#endif // INCLUDE_VARAMODGUI_H
