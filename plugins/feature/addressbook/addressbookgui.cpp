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

#include <QMessageBox>
#include <QTableWidgetItem>

#include "channel/channelapi.h"
#include "channel/channelwebapiutils.h"
#include "gui/basicfeaturesettingsdialog.h"
#include "gui/dialogpositioner.h"
#include "feature/featureuiset.h"
#include "maincore.h"

#include "ui_addressbookgui.h"
#include "addressbook.h"
#include "addressbookentrydialog.h"
#include "addressbookgui.h"

// Table item that sorts on a numeric value rather than its text
class NumericTableWidgetItem : public QTableWidgetItem
{
public:
    NumericTableWidgetItem(const QString& text, double value) :
        QTableWidgetItem(text)
    {
        setData(Qt::UserRole, value);
    }

    bool operator<(const QTableWidgetItem& other) const override
    {
        return data(Qt::UserRole).toDouble() < other.data(Qt::UserRole).toDouble();
    }
};

AddressBookGUI* AddressBookGUI::create(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature)
{
    return new AddressBookGUI(pluginAPI, featureUISet, feature);
}

void AddressBookGUI::destroy()
{
    delete this;
}

bool AddressBookGUI::handleMessage(const Message& message)
{
    if (AddressBook::MsgConfigureAddressBook::match(message))
    {
        qDebug("AddressBookGUI::handleMessage: AddressBook::MsgConfigureAddressBook");
        const AddressBook::MsgConfigureAddressBook& cfg = (const AddressBook::MsgConfigureAddressBook&) message;
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
    else if (AddressBook::MsgTuneResult::match(message))
    {
        const AddressBook::MsgTuneResult& result = (const AddressBook::MsgTuneResult&) message;
        ui->status->setText(result.getSuccess() ? QString() : result.getMessage());
        return true;
    }

    return false;
}

void AddressBookGUI::handleInputMessages()
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

void AddressBookGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applyAllSettings();
}

QByteArray AddressBookGUI::serialize() const
{
    return m_settings.serialize();
}

bool AddressBookGUI::deserialize(const QByteArray& data)
{
    if (m_settings.deserialize(data))
    {
        m_feature->setWorkspaceIndex(m_settings.m_workspaceIndex);
        displaySettings();
        applyAllSettings();
        return true;
    }

    resetToDefaults();
    return false;
}

void AddressBookGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;

    RollupContents *rollupContents = getRollupContents();

    rollupContents->saveState(m_rollupState);
    applySetting("rollupState");
}

AddressBookGUI::AddressBookGUI(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature, QWidget* parent) :
    FeatureGUI(parent),
    ui(new Ui::AddressBookGUI),
    m_addressBook(reinterpret_cast<AddressBook*>(feature)),
    m_availableChannelOrFeatureHandler(QStringList(), m_rxChannelKinds),
    m_doApplySettings(true)
{
    (void) pluginAPI;
    (void) featureUISet;

    m_feature = feature;
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/feature/addressbook/readme.md";
    RollupContents *rollupContents = getRollupContents();
    ui->setupUi(rollupContents);
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    sizeToContents();

    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));

    m_addressBook->setMessageQueueToGUI(&m_inputMessageQueue);
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));

    connect(
        &m_availableChannelOrFeatureHandler,
        &AvailableChannelOrFeatureHandler::channelsOrFeaturesChanged,
        this,
        &AddressBookGUI::channelsOrFeaturesChanged
    );
    m_availableChannelOrFeatureHandler.scanAvailableChannelsAndFeatures();

    makeUIConnections();

    m_settings.setRollupState(&m_rollupState);

    displaySettings();
    applyAllSettings();
    m_resizer.enableChildMouseTracking();
}

AddressBookGUI::~AddressBookGUI()
{
    delete ui;
}

void AddressBookGUI::setWorkspaceIndex(int index)
{
    m_settings.m_workspaceIndex = index;
    m_feature->setWorkspaceIndex(index);
}

void AddressBookGUI::blockApplySettings(bool block)
{
    m_doApplySettings = !block;
}

void AddressBookGUI::displaySettings()
{
    setTitleColor(m_settings.m_rgbColor);
    setWindowTitle(m_settings.m_title);
    setTitle(m_settings.m_title);
    blockApplySettings(true);

    updateChannelList();
    updateGroupFilter();
    updateTable();

    getRollupContents()->restoreState(m_rollupState);
    blockApplySettings(false);
}

void AddressBookGUI::onMenuDialogCalled(const QPoint &p)
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

        m_settingsKeys.append("title");
        m_settingsKeys.append("rgbColor");
        m_settingsKeys.append("useReverseAPI");
        m_settingsKeys.append("reverseAPIAddress");
        m_settingsKeys.append("reverseAPIPort");
        m_settingsKeys.append("reverseAPIFeatureSetIndex");
        m_settingsKeys.append("reverseAPIFeatureIndex");

        applySettings(m_settingsKeys);
    }

    resetContextMenuType();
}

void AddressBookGUI::applySetting(const QString& settingsKey)
{
    applySettings({settingsKey});
}

void AddressBookGUI::applySettings(const QStringList& settingsKeys, bool force)
{
    m_settingsKeys.append(settingsKeys);
    if (m_doApplySettings)
    {
        AddressBook::MsgConfigureAddressBook *msg = AddressBook::MsgConfigureAddressBook::create(m_settings, m_settingsKeys, force);
        m_addressBook->getInputMessageQueue()->push(msg);
        m_settingsKeys.clear();
    }
}

void AddressBookGUI::applyAllSettings()
{
    applySettings(QStringList(), true);
}

void AddressBookGUI::updateChannelList()
{
    m_availableChannels = m_availableChannelOrFeatureHandler.getAvailableChannelOrFeatureList();

    ui->channels->blockSignals(true);
    ui->channels->clear();

    int selectedIndex = -1;

    for (int i = 0; i < m_availableChannels.size(); ++i)
    {
        const QString longId = m_availableChannels.at(i).getLongId();
        ui->channels->addItem(longId);

        if (longId == m_settings.m_selectedChannel) {
            selectedIndex = i;
        }
    }

    if ((selectedIndex < 0) && (m_availableChannels.size() > 0)) {
        selectedIndex = 0;
    }

    if (selectedIndex >= 0)
    {
        ui->channels->setCurrentIndex(selectedIndex);
        m_settings.m_selectedChannel = m_availableChannels.at(selectedIndex).getLongId();
    }
    else
    {
        m_settings.m_selectedChannel.clear();
    }

    ui->channels->blockSignals(false);
}

void AddressBookGUI::channelsOrFeaturesChanged(const QStringList& renameFrom, const QStringList& renameTo, const QStringList& removed, const QStringList& added)
{
    (void) removed;
    (void) added;

    for (int i = 0; i < renameFrom.size() && i < renameTo.size(); ++i)
    {
        if (m_settings.m_selectedChannel == renameFrom.at(i)) {
            m_settings.m_selectedChannel = renameTo.at(i);
        }
    }

    updateChannelList();
    applySetting("selectedChannel");
}

QStringList AddressBookGUI::getGroups() const
{
    QStringList groups;

    for (const auto& entry : m_settings.m_entries)
    {
        if (!entry.m_group.isEmpty() && !groups.contains(entry.m_group)) {
            groups.append(entry.m_group);
        }
    }

    groups.sort(Qt::CaseInsensitive);
    return groups;
}

void AddressBookGUI::updateGroupFilter()
{
    ui->groupFilter->blockSignals(true);
    ui->groupFilter->clear();
    ui->groupFilter->addItem("All", "");

    for (const auto& group : getGroups()) {
        ui->groupFilter->addItem(group, group);
    }

    int index = ui->groupFilter->findData(m_settings.m_groupFilter);

    if (index < 0)
    {
        // Group no longer has any entries
        index = 0;
        m_settings.m_groupFilter.clear();
    }

    ui->groupFilter->setCurrentIndex(index);
    ui->groupFilter->blockSignals(false);
}

void AddressBookGUI::updateTable()
{
    ui->entries->setSortingEnabled(false);
    ui->entries->setRowCount(0);

    for (int i = 0; i < m_settings.m_entries.size(); i++)
    {
        const AddressBookEntry& entry = m_settings.m_entries[i];

        if (!m_settings.m_groupFilter.isEmpty() && (entry.m_group != m_settings.m_groupFilter)) {
            continue;
        }

        int row = ui->entries->rowCount();
        ui->entries->setRowCount(row + 1);

        QTableWidgetItem *nameItem = new QTableWidgetItem(entry.m_name);
        nameItem->setData(Qt::UserRole, i); // Index in to m_entries, as rows may be filtered or sorted
        ui->entries->setItem(row, ENTRY_COL_NAME, nameItem);
        ui->entries->setItem(row, ENTRY_COL_GROUP, new QTableWidgetItem(entry.m_group));
        ui->entries->setItem(row, ENTRY_COL_FREQUENCY, new NumericTableWidgetItem(QString::number(entry.m_frequency / 1e6, 'f', 6), entry.m_frequency));
        ui->entries->setItem(row, ENTRY_COL_MODE, new QTableWidgetItem(entry.m_mode));
        ui->entries->setItem(row, ENTRY_COL_CHANNEL, new QTableWidgetItem(entry.m_channelType.isEmpty() ? "Any" : entry.m_channelType));
        ui->entries->setItem(row, ENTRY_COL_BANDWIDTH, new NumericTableWidgetItem(
            entry.m_bandwidth > 0 ? QString::number(entry.m_bandwidth / 1e3, 'f', 1) : QString(), entry.m_bandwidth));
        ui->entries->setItem(row, ENTRY_COL_TX_OFFSET, new NumericTableWidgetItem(
            entry.m_txOffset != 0 ? QString::number(entry.m_txOffset / 1e6, 'f', 6) : QString(), entry.m_txOffset));
        ui->entries->setItem(row, ENTRY_COL_CTCSS, new NumericTableWidgetItem(
            entry.m_ctcss > 0.0f ? QString::number(entry.m_ctcss, 'f', 1) : QString(), entry.m_ctcss));
        QTableWidgetItem *notesItem = new QTableWidgetItem(entry.m_notes.section('\n', 0, 0));
        notesItem->setToolTip(entry.m_notes);
        ui->entries->setItem(row, ENTRY_COL_NOTES, notesItem);
    }

    ui->entries->setSortingEnabled(true);
}

int AddressBookGUI::selectedEntryIndex() const
{
    int row = ui->entries->currentRow();

    if (row < 0) {
        return -1;
    }

    QTableWidgetItem *nameItem = ui->entries->item(row, ENTRY_COL_NAME);
    return nameItem ? nameItem->data(Qt::UserRole).toInt() : -1;
}

// Add (index < 0) or edit an entry, using the dialog
void AddressBookGUI::editEntry(int index, const AddressBookEntry& entry)
{
    AddressBookEntryDialog dialog(entry, getGroups(), this);
    new DialogPositioner(&dialog, true);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (index < 0) {
        m_settings.m_entries.append(dialog.getEntry());
    } else {
        m_settings.m_entries[index] = dialog.getEntry();
    }

    updateGroupFilter();
    updateTable();
    applySettings({"entries", "groupFilter"});
}

void AddressBookGUI::tuneEntry(int index)
{
    if ((index < 0) || (index >= m_settings.m_entries.size())) {
        return;
    }

    ui->status->clear();
    AddressBook::MsgTune *msg = AddressBook::MsgTune::create(m_settings.m_entries[index], m_settings.m_selectedChannel);
    m_addressBook->getInputMessageQueue()->push(msg);
}

void AddressBookGUI::on_channels_currentIndexChanged(int index)
{
    if ((index >= 0) && (index < m_availableChannels.size()))
    {
        m_settings.m_selectedChannel = m_availableChannels.at(index).getLongId();
        applySetting("selectedChannel");
    }
}

void AddressBookGUI::on_groupFilter_currentIndexChanged(int index)
{
    m_settings.m_groupFilter = ui->groupFilter->itemData(index).toString();
    updateTable();
    applySetting("groupFilter");
}

void AddressBookGUI::on_entries_cellDoubleClicked(int row, int column)
{
    (void) row;
    (void) column;

    tuneEntry(selectedEntryIndex());
}

void AddressBookGUI::on_add_clicked()
{
    AddressBookEntry entry;
    entry.m_group = m_settings.m_groupFilter;
    editEntry(-1, entry);
}

void AddressBookGUI::on_addFromChannel_clicked()
{
    unsigned int deviceSetIndex;
    unsigned int channelIndex;

    if (!MainCore::getDeviceAndChannelIndexFromId(m_settings.m_selectedChannel, deviceSetIndex, channelIndex))
    {
        ui->status->setText("No channel selected");
        return;
    }

    ChannelAPI *channel = MainCore::instance()->getChannel(deviceSetIndex, channelIndex);
    double centerFrequency;
    int offset = 0;

    if (!channel || !ChannelWebAPIUtils::getCenterFrequency(deviceSetIndex, centerFrequency))
    {
        ui->status->setText("Failed to get channel frequency");
        return;
    }

    ChannelWebAPIUtils::getFrequencyOffset(deviceSetIndex, channelIndex, offset);

    AddressBookEntry entry;
    entry.m_group = m_settings.m_groupFilter;
    entry.m_frequency = qRound64(centerFrequency) + offset;
    entry.m_channelType = channel->getIdentifier();

    double bandwidth;

    if (ChannelWebAPIUtils::getChannelSetting(deviceSetIndex, channelIndex, "rfBandwidth", bandwidth)) {
        entry.m_bandwidth = qRound(bandwidth);
    }

    ui->status->clear();
    editEntry(-1, entry);
}

void AddressBookGUI::on_edit_clicked()
{
    int index = selectedEntryIndex();

    if (index >= 0) {
        editEntry(index, m_settings.m_entries[index]);
    }
}

void AddressBookGUI::on_remove_clicked()
{
    int index = selectedEntryIndex();

    if (index < 0) {
        return;
    }

    if (QMessageBox::question(this, "Address Book", QString("Delete %1?").arg(m_settings.m_entries[index].m_name)) != QMessageBox::Yes) {
        return;
    }

    m_settings.m_entries.removeAt(index);
    updateGroupFilter();
    updateTable();
    applySettings({"entries", "groupFilter"});
}

void AddressBookGUI::on_tune_clicked()
{
    tuneEntry(selectedEntryIndex());
}

void AddressBookGUI::makeUIConnections()
{
    connect(ui->channels, qOverload<int>(&QComboBox::currentIndexChanged), this, &AddressBookGUI::on_channels_currentIndexChanged);
    connect(ui->groupFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &AddressBookGUI::on_groupFilter_currentIndexChanged);
    connect(ui->entries, &QTableWidget::cellDoubleClicked, this, &AddressBookGUI::on_entries_cellDoubleClicked);
    connect(ui->add, &QToolButton::clicked, this, &AddressBookGUI::on_add_clicked);
    connect(ui->addFromChannel, &QToolButton::clicked, this, &AddressBookGUI::on_addFromChannel_clicked);
    connect(ui->edit, &QToolButton::clicked, this, &AddressBookGUI::on_edit_clicked);
    connect(ui->remove, &QToolButton::clicked, this, &AddressBookGUI::on_remove_clicked);
    connect(ui->tune, &QToolButton::clicked, this, &AddressBookGUI::on_tune_clicked);
}
