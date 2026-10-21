///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2021-2022 Jon Beniston, M7RCE <jon@beniston.com>                //
// Copyright (C) 2021-2022 Edouard Griffiths, F4EXB <f4exb06@gmail.com>          //
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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDesktopServices>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QInputDialog>

#include <OrbitalElements.h>
#include <Tle.h>

#include "util/units.h"
#include "satelliteselectiondialog.h"
#include "satellitetrackersgp4.h"

using namespace libsgp4;

SatelliteSelectionDialog::SatelliteSelectionDialog(SatelliteTrackerSettings *settings,
        const QHash<QString, SatNogsSatellite *>& satellites,
        QWidget* parent) :
    QDialog(parent),
    m_settings(settings),
    m_satellites(satellites),
    m_satInfo(nullptr),
    ui(new Ui::SatelliteSelectionDialog)
{
    ui->setupUi(this);
    m_networkManager = new QNetworkAccessManager();
    QObject::connect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &SatelliteSelectionDialog::networkManagerFinished
    );

    ui->hideStaleTLEs->setChecked(settings->m_hideStaleTLEs);
    ui->maxTLEAge->setValue(settings->m_maxTLEAge);
    ui->maxTLEAge->setEnabled(settings->m_hideStaleTLEs);

    // Categories, plus one for each TLE file that lists satellites
    ui->category->addItems(m_categories);
    QStringList sources;
    for (const SatNogsSatellite *sat : satellites)
    {
        for (const QString& source : sat->m_tleSources)
        {
            if (!sources.contains(source)) {
                sources.append(source);
            }
        }
    }
    sources.sort();
    for (const QString& source : sources) {
        ui->category->addItem(QString("TLE: %1").arg(source));
    }

    // Groups
    m_groups = settings->m_satelliteGroups;
    if (m_groups.contains(settings->m_satelliteGroup)) {
        m_group = settings->m_satelliteGroup;
    }
    ui->group->addItem(m_noGroup);
    ui->group->addItems(m_groups.keys());
    ui->group->setCurrentIndex(m_group.isEmpty() ? 0 : ui->group->findText(m_group));
    ui->groupDelete->setEnabled(!m_group.isEmpty());

    for (int i = 0; i < settings->m_satellites.size(); i++) {
        addSelectedSat(settings->m_satellites[i]);
    }
    updateAvailableSats();

    connect(ui->group, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SatelliteSelectionDialog::groupChanged);
    connect(ui->groupAdd, &QToolButton::clicked, this, &SatelliteSelectionDialog::groupAdd);
    connect(ui->groupDelete, &QToolButton::clicked, this, &SatelliteSelectionDialog::groupDelete);

    connect(ui->category, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SatelliteSelectionDialog::filterChanged);
    connect(ui->hideStaleTLEs, &QCheckBox::toggled, this, &SatelliteSelectionDialog::filterChanged);
    connect(ui->maxTLEAge, QOverload<int>::of(&QSpinBox::valueChanged), this, &SatelliteSelectionDialog::filterChanged);
}

const QStringList SatelliteSelectionDialog::m_categories = {
    "All", "Amateur", "FM voice", "Linear transponder", "Packet / APRS", "SSTV", "Weather", "Navigation"
};

// Categories a satellite belongs to, determined from its active transmitters and TLE files
QStringList SatelliteSelectionDialog::satelliteCategories(const SatNogsSatellite *sat)
{
    QStringList categories;
    auto add = [&categories](const QString& category) {
        if (!categories.contains(category)) {
            categories.append(category);
        }
    };

    for (const SatNogsTransmitter *transmitter : sat->m_transmitters)
    {
        if (!transmitter->m_alive) {
            continue;
        }
        QString mode = transmitter->m_mode.toUpper();
        QString description = transmitter->m_description.toUpper();

        if (transmitter->m_service == "Amateur") {
            add("Amateur");
        }
        if (mode.startsWith("FM") && (description.contains("VOICE") || description.contains("REPEATER"))) {
            add("FM voice");
        }
        if ((transmitter->m_type == "Transponder")
            && ((mode == "USB") || (mode == "LSB") || (mode == "SSB") || mode.startsWith("CW"))) {
            add("Linear transponder");
        }
        if (mode.contains("AFSK") || description.contains("APRS") || description.contains("AX.25")
            || description.contains("AX25") || description.contains("G3RUH")) {
            add("Packet / APRS");
        }
        if ((mode == "SSTV") || description.contains("SSTV")) {
            add("SSTV");
        }
        if (transmitter->m_service == "Meteorological") {
            add("Weather");
        }
        if (transmitter->m_service == "Radionavigational") {
            add("Navigation");
        }
    }

    // CelesTrak groups
    static const QStringList weatherSources = {"weather", "noaa", "goes"};
    static const QStringList navigationSources = {"gnss", "gps-ops", "glo-ops", "galileo", "beidou", "sbas", "nnss", "musson"};
    for (const QString& source : sat->m_tleSources)
    {
        if (weatherSources.contains(source)) {
            add("Weather");
        }
        if (navigationSources.contains(source)) {
            add("Navigation");
        }
        add(QString("TLE: %1").arg(source));
    }

    return categories;
}

const QString SatelliteSelectionDialog::m_noGroup = "(none)";

QStringList SatelliteSelectionDialog::selectedSats() const
{
    QStringList sats;
    for (int i = 0; i < ui->selectedSats->count(); i++) {
        sats.append(ui->selectedSats->item(i)->text());
    }
    return sats;
}

// Save selected list to the group being edited, and load the newly selected group
void SatelliteSelectionDialog::groupChanged(int index)
{
    if (!m_group.isEmpty()) {
        m_groups[m_group] = selectedSats();
    }

    m_group = index > 0 ? ui->group->itemText(index) : "";
    ui->groupDelete->setEnabled(!m_group.isEmpty());

    // Selecting "(none)" keeps the current selection
    if (!m_group.isEmpty())
    {
        ui->selectedSats->clear();
        for (const QString& name : m_groups.value(m_group)) {
            addSelectedSat(name);
        }
        updateAvailableSats();
    }
}

void SatelliteSelectionDialog::groupAdd()
{
    bool ok;
    QString name = QInputDialog::getText(this, "New group", "Name of group to create from the selected satellites:", QLineEdit::Normal, "", &ok).trimmed();

    if (!ok || name.isEmpty() || (name == m_noGroup)) {
        return;
    }
    if (m_groups.contains(name))
    {
        QMessageBox::warning(this, "New group", QString("A group called %1 already exists").arg(name));
        return;
    }

    if (!m_group.isEmpty()) {
        m_groups[m_group] = selectedSats();
    }
    m_groups.insert(name, selectedSats());
    m_group = name;

    // Keep list sorted, as QMap is
    QSignalBlocker blocker(ui->group);
    ui->group->clear();
    ui->group->addItem(m_noGroup);
    ui->group->addItems(m_groups.keys());
    ui->group->setCurrentIndex(ui->group->findText(m_group));
    ui->groupDelete->setEnabled(true);
}

void SatelliteSelectionDialog::groupDelete()
{
    if (m_group.isEmpty()) {
        return;
    }

    m_groups.remove(m_group);
    m_group = "";

    QSignalBlocker blocker(ui->group);
    ui->group->removeItem(ui->group->currentIndex());
    ui->group->setCurrentIndex(0);
    ui->groupDelete->setEnabled(false);
}

// Whether satellite's TLE is older than the maximum age set in the dialog
bool SatelliteSelectionDialog::isStale(const SatNogsSatellite *sat) const
{
    int age = sat->m_tle ? sat->m_tle->ageInDays() : -1;
    return age > ui->maxTLEAge->value();
}

// Fill list of available satellites, excluding those already selected, and those that are filtered
void SatelliteSelectionDialog::updateAvailableSats()
{
    QStringList selected;
    for (int i = 0; i < ui->selectedSats->count(); i++) {
        selected.append(ui->selectedSats->item(i)->text());
    }

    ui->availableSats->clear();

    QHashIterator<QString, SatNogsSatellite *> itr(m_satellites);
    while (itr.hasNext())
    {
        itr.next();
        QString name = itr.key();
        SatNogsSatellite *sat = itr.value();
        // Don't display decayed satellites, or those without TLEs
        if ((sat->m_status == "alive") || (sat->m_status == "in orbit") || (sat->m_status == ""))
        {
            if (sat->m_tle == nullptr) {
                continue;
            }
            // SatNOGS doesn't always update status of satellites that have re-entered, but their TLEs stop being updated
            if (ui->hideStaleTLEs->isChecked() && isStale(sat)) {
                continue;
            }
            if ((ui->category->currentIndex() > 0) && !satelliteCategories(sat).contains(ui->category->currentText())) {
                continue;
            }
            if (!selected.contains(name)) {
                ui->availableSats->addItem(name);
            }
        }
    }
    ui->availableSats->sortItems();

    // Indicate selected satellites with stale TLEs, as they may have re-entered
    for (int i = 0; i < ui->selectedSats->count(); i++) {
        updateStaleIndication(ui->selectedSats->item(i));
    }
}

void SatelliteSelectionDialog::updateStaleIndication(QListWidgetItem *item)
{
    SatNogsSatellite *sat = m_satellites.value(item->text());

    if (sat && sat->m_tle && isStale(sat))
    {
        item->setForeground(QBrush(Qt::gray));
        item->setToolTip(QString("TLE is %1 days old - satellite may have re-entered").arg(sat->m_tle->ageInDays()));
    }
    else
    {
        item->setData(Qt::ForegroundRole, QVariant());
        item->setToolTip("");
    }
}

void SatelliteSelectionDialog::addSelectedSat(const QString& name)
{
    QListWidgetItem *item = new QListWidgetItem(name);
    ui->selectedSats->addItem(item);
    updateStaleIndication(item);
}

void SatelliteSelectionDialog::filterChanged()
{
    ui->maxTLEAge->setEnabled(ui->hideStaleTLEs->isChecked());
    updateAvailableSats();
}

SatelliteSelectionDialog::~SatelliteSelectionDialog()
{
    delete m_networkManager;
    delete ui;
}

void SatelliteSelectionDialog::accept()
{
    if (!m_group.isEmpty()) {
        m_groups[m_group] = selectedSats();
    }
    m_settings->m_satelliteGroups = m_groups;
    m_settings->m_satelliteGroup = m_group;
    m_settings->m_satellites.clear();
    m_settings->m_hideStaleTLEs = ui->hideStaleTLEs->isChecked();
    m_settings->m_maxTLEAge = ui->maxTLEAge->value();

    for (int i = 0; i < ui->selectedSats->count(); i++) {
        m_settings->m_satellites.append(ui->selectedSats->item(i)->text());
    }

    QDialog::accept();
}

void SatelliteSelectionDialog::on_find_textChanged(const QString &text)
{
    QString textTrimmed = text.trimmed();
    QList<QListWidgetItem *> items = ui->availableSats->findItems(textTrimmed, Qt::MatchContains);
    if (items.size() > 0)
        ui->availableSats->setCurrentItem(items[0]);
    else
    {
        // Try alternative names
        QHashIterator<QString, SatNogsSatellite *> itr(m_satellites);
        while (itr.hasNext())
        {
            itr.next();
            SatNogsSatellite *sat = itr.value();
            if (sat->m_names.indexOf(textTrimmed) != -1)
            {
                QList<QListWidgetItem *> items = ui->availableSats->findItems(sat->m_name, Qt::MatchExactly);
                if (items.size() > 0)
                    ui->availableSats->setCurrentItem(items[0]);
                break;
            }
        }
    }
}

void SatelliteSelectionDialog::on_addSat_clicked()
{
    QList<QListWidgetItem *> items = ui->availableSats->selectedItems();
    for (int i = 0; i < items.size(); i++)
    {
        addSelectedSat(items[i]->text());
        delete items[i];
    }
}

void SatelliteSelectionDialog::on_removeSat_clicked()
{
    QList<QListWidgetItem *> items = ui->selectedSats->selectedItems();
    for (int i = 0; i < items.size(); i++)
    {
        ui->availableSats->addItem(items[i]->text());
        delete items[i];
    }
}

void SatelliteSelectionDialog::on_moveUp_clicked()
{
    QList<QListWidgetItem *> items = ui->selectedSats->selectedItems();
    for (int i = 0; i < items.size(); i++)
    {
        int row = ui->selectedSats->row(items[i]);
        if (row > 0)
        {
            QListWidgetItem *item = ui->selectedSats->takeItem(row);
            ui->selectedSats->insertItem(row - 1, item);
            ui->selectedSats->setCurrentItem(item);
        }
    }
}

void SatelliteSelectionDialog::on_moveDown_clicked()
{
    QList<QListWidgetItem *> items = ui->selectedSats->selectedItems();
    for (int i = items.size() - 1; i >= 0; i--)
    {
        int row = ui->selectedSats->row(items[i]);
        if (row < ui->selectedSats->count() - 1)
        {
            QListWidgetItem *item = ui->selectedSats->takeItem(row);
            ui->selectedSats->insertItem(row + 1, item);
            ui->selectedSats->setCurrentItem(item);
        }
    }
}

void SatelliteSelectionDialog::on_availableSats_itemDoubleClicked(QListWidgetItem *item)
{
    addSelectedSat(item->text());
    delete item;
}

void SatelliteSelectionDialog::on_selectedSats_itemDoubleClicked(QListWidgetItem *item)
{
    ui->availableSats->addItem(item->text());
    delete item;
}

void SatelliteSelectionDialog::on_availableSats_itemSelectionChanged()
{
    QList<QListWidgetItem *> items = ui->availableSats->selectedItems();
    if (items.size() > 0)
    {
        ui->selectedSats->selectionModel()->clear();
        displaySatInfo(items[0]->text());
    }
}

void SatelliteSelectionDialog::on_selectedSats_itemSelectionChanged()
{
    QList<QListWidgetItem *> items = ui->selectedSats->selectedItems();
    if (items.size() > 0)
    {
        ui->availableSats->selectionModel()->clear();
        displaySatInfo(items[0]->text());
    }
}

// Display information about the satellite from the SatNOGS database
void SatelliteSelectionDialog::displaySatInfo(const QString& name)
{
    SatNogsSatellite *sat = m_satellites[name];
    m_satInfo = sat;
    if (!sat)
    {
        // Might not be null if satellite name entered via API
        ui->satInfo->setText("");
        ui->satImage->setPixmap(QPixmap());
        return;
    }
    QStringList info;
    info.append(QString("Name: %1").arg(sat->m_name));
    if (sat->m_names.size() > 0)
        info.append(QString("Alternative names: %1").arg(sat->m_names.join(" ")));
    info.append(QString("NORAD ID: %1").arg(sat->m_noradCatId));
    if (sat->m_launched.isValid())
        info.append(QString("Launched: %1").arg(sat->m_launched.toString()));
    if (sat->m_deployed.isValid())
        info.append(QString("Deployed: %1").arg(sat->m_deployed.toString()));
    if (sat->m_decayed.isValid())
        info.append(QString("Decayed: %1").arg(sat->m_decayed.toString()));
    ui->openSatelliteWebsite->setEnabled(!sat->m_website.isEmpty());
    if (!sat->m_operator.isEmpty() && sat->m_operator != "None")
        info.append(QString("Operator: %1").arg(sat->m_operator));
    if (!sat->m_countries.isEmpty())
        info.append(QString("Countries: %1").arg(sat->m_countries));
    if (sat->m_transmitters.size() > 0)
        info.append("Modes:");
    for (int i = 0; i < sat->m_transmitters.size(); i++)
    {
        if (sat->m_transmitters[i]->m_status != "invalid")
        {
            QStringList mode;
            mode.append("  ");
            mode.append(sat->m_transmitters[i]->m_description);
            if (sat->m_transmitters[i]->m_downlinkHigh > 0)
                mode.append(QString("D: %1").arg(SatNogsTransmitter::getFrequencyRangeText(sat->m_transmitters[i]->m_downlinkLow, sat->m_transmitters[i]->m_downlinkHigh)));
            else if (sat->m_transmitters[i]->m_downlinkLow > 0)
                mode.append(QString("D: %1").arg(SatNogsTransmitter::getFrequencyText(sat->m_transmitters[i]->m_downlinkLow)));
            if (sat->m_transmitters[i]->m_uplinkHigh > 0)
                mode.append(QString("U: %1").arg(SatNogsTransmitter::getFrequencyRangeText(sat->m_transmitters[i]->m_uplinkLow, sat->m_transmitters[i]->m_uplinkHigh)));
            else if (sat->m_transmitters[i]->m_uplinkLow > 0)
                mode.append(QString("U: %1").arg(SatNogsTransmitter::getFrequencyText(sat->m_transmitters[i]->m_uplinkLow)));
            info.append(mode.join(" "));
        }
    }
    if (sat->m_tle != nullptr)
    {
        QDateTime epoch = sat->m_tle->epoch();
        if (epoch.isValid()) {
            info.append(QString("%1 epoch: %2 (%3 days old)").arg(sat->m_tle->isOMM() ? "OMM" : "TLE").arg(epoch.toString(Qt::ISODate)).arg(sat->m_tle->ageInDays()));
        }
        try
        {
            info.append("Orbit:");
            Tle tle = createTle(*sat->m_tle);
            OrbitalElements ele(tle);
            info.append(QString("  Period: %1 mins").arg(ele.Period()));
            info.append(QString("  Inclination: %1%2").arg(Units::radiansToDegrees(ele.Inclination())).arg(QChar(0xb0)));
            info.append(QString("  Eccentricity: %1").arg(ele.Eccentricity()));
        }
        catch (TleException& tlee)
        {
            qDebug() << "SatelliteSelectionDialog::displaySatInfo: TleException " << tlee.what();
        }
    }

    ui->satInfo->setText(info.join("\n"));
    if (!sat->m_image.isEmpty())
    {
        if (sat->m_image.startsWith("http")) {
            m_networkManager->get(QNetworkRequest(QUrl(sat->m_image)));
        } else {
            m_networkManager->get(QNetworkRequest(QUrl("https://db-satnogs.freetls.fastly.net/media/" + sat->m_image)));
        }
    }
    else
    {
        ui->satImage->setPixmap(QPixmap());
    }
}

// Open the Satellite's webpage
void SatelliteSelectionDialog::on_openSatelliteWebsite_clicked()
{
    if ((m_satInfo != nullptr) && (!m_satInfo->m_website.isEmpty()))
        QDesktopServices::openUrl(QUrl(m_satInfo->m_website));
}

// Open SatNOGS observations website for the selected satellite
void SatelliteSelectionDialog::on_openSatNogsObservations_clicked()
{
    if (m_satInfo != nullptr)
        QDesktopServices::openUrl(QUrl(QString("https://network.satnogs.org/observations/?norad=%1").arg(m_satInfo->m_noradCatId)));
}

void SatelliteSelectionDialog::networkManagerFinished(QNetworkReply *reply)
{
    QNetworkReply::NetworkError replyError = reply->error();

    if (replyError)
    {
        qWarning() << "SatelliteSelectionDialog::networkManagerFinished:"
                << " error(" << (int) replyError
                << "): " << replyError
                << ": " << reply->errorString();
    }
    else
    {
        // Read image data and display it
        QByteArray imageData = reply->readAll();
        QPixmap pixmap;
        if (pixmap.loadFromData(imageData))
            ui->satImage->setPixmap(pixmap.scaled( ui->satImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        else
            qDebug() << "SatelliteSelectionDialog::networkManagerFinished: Failed to load pixmap from image data";
    }

    reply->deleteLater();
}
