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

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "maincore.h"
#include "plugin/plugininterface.h"
#include "plugin/pluginmanager.h"

#include "addressbookentrydialog.h"

AddressBookEntryDialog::AddressBookEntryDialog(const AddressBookEntry& entry, const QStringList& groups, QWidget* parent) :
    QDialog(parent),
    m_entry(entry)
{
    setWindowTitle(entry.m_name.isEmpty() ? "Add Entry" : "Edit Entry");

    QVBoxLayout *layout = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout();
    layout->addLayout(form);

    m_name = new QLineEdit(entry.m_name);
    m_name->setToolTip("Name of the entry. E.g. a callsign, repeater or satellite name");
    form->addRow("Name", m_name);

    m_group = new QComboBox();
    m_group->setEditable(true);
    m_group->addItems(groups);
    m_group->setCurrentText(entry.m_group);
    m_group->setToolTip("Group to file the entry under. Type a new name to create a group");
    form->addRow("Group", m_group);

    m_frequency = new QDoubleSpinBox();
    m_frequency->setDecimals(6);
    m_frequency->setRange(0.0, 100000.0);
    m_frequency->setSuffix(" MHz");
    m_frequency->setValue(entry.m_frequency / 1e6);
    m_frequency->setToolTip("Receive frequency in MHz");
    form->addRow("Frequency", m_frequency);

    m_mode = new QComboBox();
    m_mode->setEditable(true);
    m_mode->addItems({"NFM", "WFM", "AM", "USB", "LSB", "CW", "DATA", "SSTV", "PKT", "DMR", "D-STAR", "C4FM"});
    m_mode->setCurrentText(entry.m_mode);
    m_mode->setToolTip("Mode. Free text, for display and filtering");
    form->addRow("Mode", m_mode);

    m_channelType = new QComboBox();
    m_channelType->addItem("Any", "");
    const PluginAPI::ChannelRegistrations *rxChannels = MainCore::instance()->getPluginManager()->getRxChannelRegistrations();

    for (const auto& registration : *rxChannels) {
        m_channelType->addItem(registration.m_plugin->getPluginDescriptor().displayedName, registration.m_channelId);
    }

    int channelTypeIndex = m_channelType->findData(entry.m_channelType);

    if ((channelTypeIndex < 0) && !entry.m_channelType.isEmpty())
    {
        // Plugin not loaded, but keep the type so it isn't lost on save
        m_channelType->addItem(entry.m_channelType, entry.m_channelType);
        channelTypeIndex = m_channelType->count() - 1;
    }

    m_channelType->setCurrentIndex(channelTypeIndex < 0 ? 0 : channelTypeIndex);
    m_channelType->setToolTip("Type of channel this entry can be tuned on. Any to allow all channel types");
    form->addRow("Channel", m_channelType);

    m_bandwidth = new QDoubleSpinBox();
    m_bandwidth->setDecimals(1);
    m_bandwidth->setRange(0.0, 20000.0);
    m_bandwidth->setSuffix(" kHz");
    m_bandwidth->setSpecialValueText("Unchanged");
    m_bandwidth->setValue(entry.m_bandwidth / 1e3);
    m_bandwidth->setToolTip("Channel RF bandwidth in kHz. 0 to leave the channel's bandwidth unchanged");
    form->addRow("Bandwidth", m_bandwidth);

    m_txOffset = new QDoubleSpinBox();
    m_txOffset->setDecimals(6);
    m_txOffset->setRange(-100000.0, 100000.0);
    m_txOffset->setSuffix(" MHz");
    m_txOffset->setValue(entry.m_txOffset / 1e6);
    m_txOffset->setToolTip("Transmit frequency minus receive frequency in MHz. E.g. -0.6 for a 2m repeater with a negative shift");
    form->addRow("Tx offset", m_txOffset);

    m_ctcss = new QDoubleSpinBox();
    m_ctcss->setDecimals(1);
    m_ctcss->setRange(0.0, 300.0);
    m_ctcss->setSuffix(" Hz");
    m_ctcss->setSpecialValueText("None");
    m_ctcss->setValue(entry.m_ctcss);
    m_ctcss->setToolTip("CTCSS tone in Hz");
    form->addRow("CTCSS", m_ctcss);

    m_azimuth = new QDoubleSpinBox();
    m_azimuth->setDecimals(1);
    m_azimuth->setRange(-1.0, 360.0);
    m_azimuth->setSuffix(" deg");
    m_azimuth->setSpecialValueText("None");
    m_azimuth->setValue(entry.m_azimuth < 0.0f ? -1.0 : entry.m_azimuth);
    m_azimuth->setToolTip("Antenna azimuth in degrees. When set, tuning the entry also points the rotator (GS-232 Rotator Controller) to it");
    form->addRow("Azimuth", m_azimuth);

    m_notes = new QPlainTextEdit(entry.m_notes);
    m_notes->setToolTip("Notes");
    form->addRow("Notes", m_notes);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &AddressBookEntryDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &AddressBookEntryDialog::reject);
    layout->addWidget(buttons);
}

void AddressBookEntryDialog::accept()
{
    if (m_name->text().trimmed().isEmpty())
    {
        QMessageBox::warning(this, "Address Book", "Please enter a name");
        return;
    }

    QDialog::accept();
}

AddressBookEntry AddressBookEntryDialog::getEntry() const
{
    AddressBookEntry entry = m_entry;

    entry.m_name = m_name->text().trimmed();
    entry.m_group = m_group->currentText().trimmed();
    entry.m_frequency = qRound64(m_frequency->value() * 1e6);
    entry.m_mode = m_mode->currentText().trimmed();
    entry.m_channelType = m_channelType->currentData().toString();
    entry.m_bandwidth = qRound(m_bandwidth->value() * 1e3);
    entry.m_txOffset = qRound64(m_txOffset->value() * 1e6);
    entry.m_ctcss = m_ctcss->value();
    entry.m_azimuth = m_azimuth->value() < 0.0 ? -1.0f : m_azimuth->value();
    entry.m_notes = m_notes->toPlainText();

    return entry;
}
