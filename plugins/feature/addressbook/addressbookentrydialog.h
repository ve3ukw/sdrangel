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

#ifndef INCLUDE_FEATURE_ADDRESSBOOKENTRYDIALOG_H_
#define INCLUDE_FEATURE_ADDRESSBOOKENTRYDIALOG_H_

#include <QDialog>

#include "addressbookentry.h"

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

/// Dialog to add or edit an address book entry
class AddressBookEntryDialog : public QDialog
{
    Q_OBJECT

public:
    AddressBookEntryDialog(const AddressBookEntry& entry, const QStringList& groups, QWidget* parent = nullptr);

    AddressBookEntry getEntry() const;

private:
    AddressBookEntry m_entry;
    QLineEdit *m_name;
    QComboBox *m_group;
    QDoubleSpinBox *m_frequency;
    QComboBox *m_mode;
    QComboBox *m_channelType;
    QDoubleSpinBox *m_bandwidth;
    QDoubleSpinBox *m_txOffset;
    QDoubleSpinBox *m_ctcss;
    QPlainTextEdit *m_notes;

    void accept() override;
};

#endif // INCLUDE_FEATURE_ADDRESSBOOKENTRYDIALOG_H_
