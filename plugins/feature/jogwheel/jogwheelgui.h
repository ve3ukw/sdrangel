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

#ifndef INCLUDE_FEATURE_JOGWHEELGUI_H_
#define INCLUDE_FEATURE_JOGWHEELGUI_H_

#include <QTimer>

#include "availablechannelorfeaturehandler.h"
#include "feature/featuregui.h"
#include "util/messagequeue.h"
#include "settings/rollupstate.h"

#include "jogwheelsettings.h"

class PluginAPI;
class FeatureUISet;
class JogWheel;
class Feature;
class QComboBox;
class DeviceAPI;

namespace Ui {
    class JogWheelGUI;
}

class JogWheelGUI : public FeatureGUI {
    Q_OBJECT
public:
    static JogWheelGUI* create(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature);
    void destroy() override;

    void resetToDefaults() override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data) override;
    MessageQueue *getInputMessageQueue() override { return &m_inputMessageQueue; }
    void setWorkspaceIndex(int index) override;
    int getWorkspaceIndex() const override { return m_settings.m_workspaceIndex; }
    void setGeometryBytes(const QByteArray& blob) override { m_settings.m_geometryBytes = blob; }
    QByteArray getGeometryBytes() const override { return m_settings.m_geometryBytes; }

private:
    Ui::JogWheelGUI* ui;
    JogWheel* m_jogWheel;
    JogWheelSettings m_settings;
    QStringList m_settingsKeys;
    RollupState m_rollupState;
    bool m_doApplySettings;
    MessageQueue m_inputMessageQueue;
    AvailableChannelOrFeatureHandler m_availableChannelHandler;
    QTimer m_pollTimer;
    QTimer m_multiplierTimer;

    static constexpr const char* m_channelKinds = "RTM";
    static const int m_pollIntervalMs = 200;

    explicit JogWheelGUI(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature, QWidget* parent = nullptr);
    ~JogWheelGUI() override;

    void blockApplySettings(bool block);
    void applySetting(const QString& settingsKey);
    void applySettings(const QStringList& settingsKeys, bool force = false);
    void applyAllSettings();
    void displaySettings();
    void displayStep();
    void updateTargetLists();
    void fillTargets(QComboBox *combo, const QString& selected, bool withNone);
    bool handleMessage(const Message& message);
    void makeUIConnections();
    static bool sameTarget(const QString& a, const QString& b);
    static QString formatFrequency(qint64 frequency);
    static QString formatDelta(qint64 deltaHz);

private slots:
    void onMenuDialogCalled(const QPoint &p);
    void onWidgetRolled(QWidget* widget, bool rollDown);
    void handleInputMessages();
    void channelsChanged(const QStringList& renameFrom, const QStringList& renameTo, const QStringList& removed, const QStringList& added);
    void deviceSetsChanged();
    void on_target_currentIndexChanged(int index);
    void on_linkedTarget_currentIndexChanged(int index);
    void on_linkSense_currentIndexChanged(int index);
    void on_step_currentIndexChanged(int index);
    void on_acceleration_toggled(bool checked);
    void on_udpEnabled_toggled(bool checked);
    void on_udpAddress_editingFinished();
    void on_udpPort_editingFinished();
    void on_resetTotal_clicked();
    void knobTurned(int detents);
    void pollFrequencies();
};

#endif // INCLUDE_FEATURE_JOGWHEELGUI_H_
