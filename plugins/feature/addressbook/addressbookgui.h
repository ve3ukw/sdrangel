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

#ifndef INCLUDE_FEATURE_ADDRESSBOOKGUI_H_
#define INCLUDE_FEATURE_ADDRESSBOOKGUI_H_

#include "availablechannelorfeaturehandler.h"
#include "feature/featuregui.h"
#include "util/messagequeue.h"
#include "settings/rollupstate.h"

#include "addressbooksettings.h"

class PluginAPI;
class FeatureUISet;
class AddressBook;
class Feature;

namespace Ui {
class AddressBookGUI;
}

class AddressBookGUI : public FeatureGUI
{
    Q_OBJECT

public:
    static AddressBookGUI* create(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature);
    void destroy() override;

    bool handleMessage(const Message& message);

    void resetToDefaults() override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data) override;
    MessageQueue *getInputMessageQueue() override { return &m_inputMessageQueue; }
    void setWorkspaceIndex(int index) override;
    int getWorkspaceIndex() const override { return m_settings.m_workspaceIndex; }
    void setGeometryBytes(const QByteArray& blob) override { m_settings.m_geometryBytes = blob; }
    QByteArray getGeometryBytes() const override { return m_settings.m_geometryBytes; }

private:
    enum EntryColumn {
        ENTRY_COL_NAME,
        ENTRY_COL_GROUP,
        ENTRY_COL_FREQUENCY,
        ENTRY_COL_MODE,
        ENTRY_COL_CHANNEL,
        ENTRY_COL_BANDWIDTH,
        ENTRY_COL_TX_OFFSET,
        ENTRY_COL_CTCSS,
        ENTRY_COL_NOTES
    };

    Ui::AddressBookGUI* ui;
    AddressBook* m_addressBook;
    AddressBookSettings m_settings;
    QStringList m_settingsKeys;
    RollupState m_rollupState;
    MessageQueue m_inputMessageQueue;
    AvailableChannelOrFeatureHandler m_availableChannelOrFeatureHandler;
    AvailableChannelOrFeatureList m_availableChannels;
    bool m_doApplySettings;

    static constexpr const char* m_rxChannelKinds = "R";

    explicit AddressBookGUI(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature, QWidget* parent = nullptr);
    ~AddressBookGUI() override;

    void makeUIConnections();
    void blockApplySettings(bool block);
    void displaySettings();
    void applySetting(const QString& settingsKey);
    void applySettings(const QStringList& settingsKeys, bool force = false);
    void applyAllSettings();
    void updateChannelList();
    void updateGroupFilter();
    void updateTable();
    QStringList getGroups() const;
    int selectedEntryIndex() const;
    void editEntry(int index, const AddressBookEntry& entry);
    void tuneEntry(int index);

private slots:
    void onMenuDialogCalled(const QPoint &p);
    void onWidgetRolled(QWidget* widget, bool rollDown);
    void handleInputMessages();
    void channelsOrFeaturesChanged(const QStringList& renameFrom, const QStringList& renameTo, const QStringList& removed, const QStringList& added);
    void on_channels_currentIndexChanged(int index);
    void on_groupFilter_currentIndexChanged(int index);
    void on_entries_cellDoubleClicked(int row, int column);
    void on_add_clicked();
    void on_addFromChannel_clicked();
    void on_edit_clicked();
    void on_remove_clicked();
    void on_tune_clicked();
};

#endif // INCLUDE_FEATURE_ADDRESSBOOKGUI_H_
