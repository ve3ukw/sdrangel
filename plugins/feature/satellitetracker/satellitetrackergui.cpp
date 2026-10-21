///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2021-2023 Jon Beniston, M7RCE <jon@beniston.com>                //
// Copyright (C) 2021-2022 Edouard Griffiths, F4EXB <f4exb06@gmail.com>          //
// Copyright (C) 2021 DreamNik <dreamnik@mail.ru>                                //
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

#include <cmath>
#include <algorithm>
#include <QMessageBox>
#include <QActionGroup>
#include <QSignalBlocker>
#include <QLineEdit>

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>

#include "channel/channelwebapiutils.h"
#include "feature/featureset.h"
#include "feature/featureuiset.h"
#include "feature/featureutils.h"
#include "feature/featurewebapiutils.h"
#include "gui/messagedialog.h"
#include "gui/basicfeaturesettingsdialog.h"
#include "gui/dialogpositioner.h"
#include "util/units.h"
#include "device/deviceapi.h"
#include "device/deviceset.h"
#include "maincore.h"
#include "channel/channelapi.h"
#include "plugin/pluginmanager.h"

#include "ui_satellitetrackergui.h"
#include "satellitetracker.h"
#include "satellitetrackergui.h"
#include "satellitetrackerreport.h"
#include "satellitetrackersettingsdialog.h"
#include "satelliteselectiondialog.h"
#include "satelliteradiocontroldialog.h"
#include "satellitetrackersgp4.h"

SatelliteTrackerGUI* SatelliteTrackerGUI::create(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature)
{
    SatelliteTrackerGUI* gui = new SatelliteTrackerGUI(pluginAPI, featureUISet, feature);
    return gui;
}

void SatelliteTrackerGUI::destroy()
{
    delete this;
}

void SatelliteTrackerGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applySettings(true);
}

QByteArray SatelliteTrackerGUI::serialize() const
{
    return m_settings.serialize();
}

bool SatelliteTrackerGUI::deserialize(const QByteArray& data)
{
    if (m_settings.deserialize(data))
    {
        m_feature->setWorkspaceIndex(m_settings.m_workspaceIndex);
        updateSelectedSats();
        displaySettings();
        applySettings(true);
        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

QString SatelliteTrackerGUI::convertDegreesToText(double degrees)
{
    if (m_settings.m_azElUnits == SatelliteTrackerSettings::DMS)
        return Units::decimalDegreesToDegreeMinutesAndSeconds(degrees);
    else if (m_settings.m_azElUnits == SatelliteTrackerSettings::DM)
        return Units::decimalDegreesToDegreesAndMinutes(degrees);
    else if (m_settings.m_azElUnits == SatelliteTrackerSettings::D)
        return Units::decimalDegreesToDegrees(degrees);
    else
        return QString("%1").arg(degrees, 0, 'f', 2);
}

bool SatelliteTrackerGUI::handleMessage(const Message& message)
{
    if (SatelliteTracker::MsgConfigureSatelliteTracker::match(message))
    {
        qDebug("SatelliteTrackerGUI::handleMessage: SatelliteTracker::MsgConfigureSatelliteTracker");
        const SatelliteTracker::MsgConfigureSatelliteTracker& cfg = (SatelliteTracker::MsgConfigureSatelliteTracker&) message;

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
    else if (SatelliteTrackerReport::MsgReportSat::match(message))
    {
        SatelliteTrackerReport::MsgReportSat& satReport = (SatelliteTrackerReport::MsgReportSat&) message;
        SatelliteState *satState = satReport.getSatelliteState();

        if (satState->m_name == m_settings.m_target)
        {
            delete m_targetSatState;
            m_targetSatState = satState;

            ui->azimuth->setText(convertDegreesToText(satState->m_azimuth));
            ui->elevation->setText(convertDegreesToText(satState->m_elevation));
            plotChart();

            if (satState->m_passes.size() > 0)
            {
                const SatellitePass &pass = satState->m_passes[0];
                bool geostationary = !pass.m_aos.isValid() && !pass.m_los.isValid();

                if ((m_nextTargetAOS != pass.m_aos) || (m_nextTargetLOS != pass.m_los) || (geostationary != m_geostationarySatVisible))
                {
                    m_nextTargetAOS = pass.m_aos;
                    m_nextTargetLOS = pass.m_los;
                    m_geostationarySatVisible = geostationary;
                    updateTimeToAOS();
                }
            }
        }

        updateTable(satState);

        if (satState->m_name != m_settings.m_target) {
            delete satState;
        }

        return true;
    }
    else if (SatelliteTrackerReport::MsgReportAOS::match(message))
    {
        SatelliteTrackerReport::MsgReportAOS& aosReport = (SatelliteTrackerReport::MsgReportAOS&) message;
        aos(aosReport.getSpeech());
        return true;
    }
    else if (SatelliteTrackerReport::MsgReportTarget::match(message))
    {
        SatelliteTrackerReport::MsgReportTarget& targetReport = (SatelliteTrackerReport::MsgReportTarget&) message;
        setTarget(targetReport.getName());
        return true;
    }
    else if (SatelliteTrackerReport::MsgReportTune::match(message))
    {
        // Track how far user has retuned from the transmitter's frequency
        SatelliteTrackerReport::MsgReportTune& tuneReport = (SatelliteTrackerReport::MsgReportTune&) message;
        if (!m_tuneSatellite.isEmpty() && (tuneReport.getTuneId() == m_tuneId)) {
            m_tuneOffset = tuneReport.getFrequency() - m_tuneBaseFrequency;
        }
        return true;
    }
    else if (SatelliteTrackerReport::MsgReportLOS::match(message))
    {
        SatelliteTrackerReport::MsgReportLOS& losReport = (SatelliteTrackerReport::MsgReportLOS&) message;
        los(losReport.getSpeech());
        return true;
    }
    else if (SatelliteTracker::MsgSatData::match(message))
    {
        SatelliteTracker::MsgSatData& satData = (SatelliteTracker::MsgSatData&) message;
        m_satellites = satData.getSatellites();
        // Remove satellites that no longer exist
        QMutableListIterator<QString> itr(m_settings.m_satellites);

        while (itr.hasNext())
        {
            QString satellite = itr.next();
            if (!m_satellites.contains(satellite))
                itr.remove();
        }

        if (!m_satellites.contains(m_settings.m_target)) {
            setTarget("");
        }

        // Update GUI
        updateSelectedSats();

        return true;
    }
    else if (SatelliteTracker::MsgError::match(message))
    {
        SatelliteTracker::MsgError& errorMsg = (SatelliteTracker::MsgError&) message;
        QString error = errorMsg.getError();
        MessageDialog::critical(this, "Satellite Tracker", error);
        return true;
    }

    return false;
}

// Call when m_settings.m_satellites changes
void SatelliteTrackerGUI::updateSelectedSats()
{
    // Remove unselects sats from target combo and table
    for (int i = 0; i < ui->target->count(); )
    {
        QString name = ui->target->itemText(i);
        int idx = m_settings.m_satellites.indexOf(name);

        if (idx == -1)
        {
            ui->target->removeItem(i);
            QList<QTableWidgetItem *> matches = ui->satTable->findItems(name, Qt::MatchExactly);

            for (int j = 0; j < matches.length(); j++) {
                ui->satTable->removeRow(ui->satTable->row(matches[j]));
            }
        }
        else
        {
            i++;
        }
    }

    // Add new satellites to target combo
    for (int i = 0; i < m_settings.m_satellites.size(); i++)
    {
        if (ui->target->findText(m_settings.m_satellites[i], Qt::MatchExactly) == -1) {
            ui->target->addItem(m_settings.m_satellites[i]);
        }
    }
    // Select current target, if it still exists
    int idx = ui->target->findText(m_settings.m_target);

    if (idx != -1) {
        ui->target->setCurrentIndex(idx);
    } else {
        setTarget("");
    }
}

void SatelliteTrackerGUI::handleInputMessages()
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

void SatelliteTrackerGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;

    RollupContents *rollupContents = getRollupContents();
    rollupContents->saveState(m_rollupState);
}

SatelliteTrackerGUI::SatelliteTrackerGUI(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature, QWidget* parent) :
    FeatureGUI(parent),
    ui(new Ui::SatelliteTrackerGUI),
    m_pluginAPI(pluginAPI),
    m_featureUISet(featureUISet),
    m_doApplySettings(true),
    m_lastUpdatingSatData(false),
    m_targetSatState(nullptr),
    m_plotPass(0),
    m_lineChart(nullptr),
    m_polarChart(nullptr),
    m_geostationarySatVisible(false),
    m_tuneFrequency(0),
    m_tuneDeviceSetIndex(-1),
    m_tuneChannelIndex(-1),
    m_tuneMode(TUNE_OTHER),
    m_tuneChannel(nullptr),
    m_createdChannel(nullptr),
    m_createdChannelDeviceSetIndex(-1),
    m_tuneId(0),
    m_tuneBaseFrequency(0),
    m_tuneOffset(0),
    m_tuneCTCSS(0.0f)
{
    m_pendingTune.m_active = false;
    m_feature = feature;
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/feature/satellitetracker/readme.md";
    RollupContents *rollupContents = getRollupContents();
	ui->setupUi(rollupContents);
    rollupContents->arrangeRollups();
	connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));

    m_satelliteTracker = reinterpret_cast<SatelliteTracker*>(feature);
    m_satelliteTracker->setMessageQueueToGUI(&m_inputMessageQueue);

    m_settings.setRollupState(&m_rollupState);

    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));

    connect(&m_statusTimer, SIGNAL(timeout()), this, SLOT(updateStatus()));
    m_statusTimer.start(1000);
    connect(m_satelliteTracker, &Feature::stateChanged, this, &SatelliteTrackerGUI::updateFeatureState);
    updateFeatureState();

    connect(&m_redrawTimer, &QTimer::timeout, this, &SatelliteTrackerGUI::plotChart);

    // Initialise charts
    m_emptyChart.layout()->setContentsMargins(0, 0, 0, 0);
    m_emptyChart.setMargins(QMargins(1, 1, 1, 1));
    ui->passChart->setChart(&m_emptyChart);
    ui->passChart->setRenderHint(QPainter::Antialiasing);

    ui->dateTime->setDateTime(m_satelliteTracker->currentDateTime());
    ui->deviceFeatureSelect->setVisible(false);

    resizeTable();
    // Allow user to reorder columns
    ui->satTable->horizontalHeader()->setSectionsMovable(true);
    // Allow user to sort table by clicking on headers
    ui->satTable->setSortingEnabled(true);
    // Add context menu to allow hiding/showing of columns
    menu = new QMenu(ui->satTable);

    for (int i = 0; i < ui->satTable->horizontalHeader()->count(); i++)
    {
        QString text = ui->satTable->horizontalHeaderItem(i)->text();
        menu->addAction(createCheckableItem(text, i, true));
    }

    ui->satTable->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->satTable->horizontalHeader(), SIGNAL(customContextMenuRequested(QPoint)), SLOT(columnSelectMenu(QPoint)));
    // Context menu to tune a channel to one of the satellite's transmitters
    ui->satTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->satTable, &QTableWidget::customContextMenuRequested, this, &SatelliteTrackerGUI::satTable_customContextMenuRequested);
    connect(MainCore::instance(), &MainCore::channelAdded, this, &SatelliteTrackerGUI::channelAdded);
    connect(MainCore::instance(), &MainCore::channelRemoved, this, &SatelliteTrackerGUI::channelRemoved);
    // Get signals when columns change
    connect(ui->satTable->horizontalHeader(), SIGNAL(sectionMoved(int, int, int)), SLOT(satTable_sectionMoved(int, int, int)));
    connect(ui->satTable->horizontalHeader(), SIGNAL(sectionResized(int, int, int)), SLOT(satTable_sectionResized(int, int, int)));

#ifdef QT_TEXTTOSPEECH_FOUND
    m_speech = new QTextToSpeech(this);
#endif

    displaySettings();
    applySettings(true);
    makeUIConnections();
    m_resizer.enableChildMouseTracking();

    // Get initial list of satellites
    on_updateSatData_clicked();

    // Use My Position from preferences, if none set
    if ((m_settings.m_latitude == 0.0) && (m_settings.m_longitude == 0.0)) {
        on_useMyPosition_clicked();
    }
}

SatelliteTrackerGUI::~SatelliteTrackerGUI()
{
    delete ui;
}

void SatelliteTrackerGUI::setWorkspaceIndex(int index)
{
    m_settings.m_workspaceIndex = index;
    m_settingsKeys.append("workspaceIndex");
    m_feature->setWorkspaceIndex(index);
}

void SatelliteTrackerGUI::blockApplySettings(bool block)
{
    m_doApplySettings = !block;
}

void SatelliteTrackerGUI::displaySettings()
{
    setTitleColor(m_settings.m_rgbColor);
    setWindowTitle(m_settings.m_title);
    setTitle(m_settings.m_title);
    blockApplySettings(true);
    ui->latitude->setValue(m_settings.m_latitude);
    ui->longitude->setValue(m_settings.m_longitude);

    updateSatelliteGroups();

    ui->target->blockSignals(true);
    ui->target->clear();
    for (const QString& s : m_settings.m_satellites) {
        ui->target->addItem(s);
    }
    ui->target->blockSignals(false);
    ui->target->setCurrentIndex(ui->target->findText(m_settings.m_target));

    ui->dateTime->setDateTime(QDateTime::fromString(m_settings.m_dateTime, Qt::ISODateWithMs));
    ui->dateTimeSelect->setCurrentIndex((int)m_settings.m_dateTimeSelect);
    ui->dateTime->setVisible(m_settings.m_dateTimeSelect == SatelliteTrackerSettings::CUSTOM);
    ui->autoTarget->setChecked(m_settings.m_autoTarget);
    ui->darkTheme->setChecked(m_settings.m_chartsDarkTheme);
    ui->satTable->horizontalHeader()->setSortIndicator(m_settings.m_columnSort, m_settings.m_columnSortOrder);
    getRollupContents()->restoreState(m_rollupState);
    plotChart();
    blockApplySettings(false);
}

void SatelliteTrackerGUI::onMenuDialogCalled(const QPoint &p)
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

        applySettings();
    }

    resetContextMenuType();
}

void SatelliteTrackerGUI::aos(const QString &speech)
{
    // Call plotChart() to start the periodic updates with sat position in polar chart
    plotChart();
    // Give speech notification of pass
    if (!speech.isEmpty()) 
    {
#ifdef QT_TEXTTOSPEECH_FOUND
        m_speech->say(speech);
#else
        qWarning() << "SatelliteTrackerGUI::aos: No TextToSpeech: " << speech;
#endif
    }
}

void SatelliteTrackerGUI::los(const QString &speech)
{
    // Give speech notification of end of pass
    if (!speech.isEmpty()) 
    {
#ifdef QT_TEXTTOSPEECH_FOUND
        m_speech->say(speech);
#else
        qWarning() << "SatelliteTrackerGUI::los: No TextToSpeech: " << speech;
#endif
    }
}

void SatelliteTrackerGUI::on_startStop_toggled(bool checked)
{
    if (m_doApplySettings)
    {
        SatelliteTracker::MsgStartStop *message = SatelliteTracker::MsgStartStop::create(checked);
        m_satelliteTracker->getInputMessageQueue()->push(message);
    }
}

void SatelliteTrackerGUI::on_latitude_valueChanged(double value)
{
    m_settings.m_latitude = value;
    m_settingsKeys.append("latitude");
    applySettings();
    plotChart();
}

void SatelliteTrackerGUI::on_longitude_valueChanged(double value)
{
    m_settings.m_longitude = value;
    m_settingsKeys.append("longitude");
    applySettings();
    plotChart();
}

void SatelliteTrackerGUI::setTarget(const QString& target)
{
    if (target != m_settings.m_target)
    {
        m_settings.m_target = target;
        m_settingsKeys.append("target");
        ui->azimuth->setText("");
        ui->elevation->setText("");
        ui->aos->setText("");
        ui->target->setCurrentIndex(ui->target->findText(m_settings.m_target));
        m_nextTargetAOS = QDateTime();
        m_nextTargetLOS = QDateTime();
        m_geostationarySatVisible = false;
        applySettings();
        delete m_targetSatState;
        m_targetSatState = nullptr;
        m_plotPass = 0;
        ui->passLabel->setText(QString("%1").arg(m_plotPass));
        plotChart();
    }
}

void SatelliteTrackerGUI::on_target_currentTextChanged(const QString &text)
{
    setTarget(text);
}

void SatelliteTrackerGUI::on_useMyPosition_clicked(bool checked)
{
    (void) checked;
    double stationLatitude = MainCore::instance()->getSettings().getLatitude();
    double stationLongitude = MainCore::instance()->getSettings().getLongitude();
    double stationAltitude = MainCore::instance()->getSettings().getAltitude();

    ui->latitude->setValue(stationLatitude);
    ui->longitude->setValue(stationLongitude);
    m_settings.m_heightAboveSeaLevel = stationAltitude;
    m_settingsKeys.append("heightAboveSeaLevel");
    applySettings();
    plotChart();
}

// Show settings dialog
void SatelliteTrackerGUI::on_displaySettings_clicked()
{
    SatelliteTrackerSettingsDialog dialog(&m_settings);
    new DialogPositioner(&dialog, true);
    if (dialog.exec() == QDialog::Accepted)
    {
        m_settingsKeys.append("heightAboveSeaLevel");
        m_settingsKeys.append("predictionPeriod");
        m_settingsKeys.append("passStartTime");
        m_settingsKeys.append("passFinishTime");
        m_settingsKeys.append("minAOSElevation");
        m_settingsKeys.append("minPassElevation");
        m_settingsKeys.append("rotatorMaxAzimuth");
        m_settingsKeys.append("rotatorMaxElevation");
        m_settingsKeys.append("aosSpeech");
        m_settingsKeys.append("losSpeech");
        m_settingsKeys.append("aosCommand");
        m_settingsKeys.append("losCommand");
        m_settingsKeys.append("updatePeriod");
        m_settingsKeys.append("dopplerPeriod");
        m_settingsKeys.append("defaultFrequency");
        m_settingsKeys.append("azElUnits");
        m_settingsKeys.append("groundTrackPoints");
        m_settingsKeys.append("drawRotators");
        m_settingsKeys.append("dateFormat");
        m_settingsKeys.append("utc");
        m_settingsKeys.append("tles");
        m_settingsKeys.append("replayEnabled");
        m_settingsKeys.append("replayStartDateTime");
        m_settingsKeys.append("sendTimeToMap");
        applySettings();
        plotChart();
    }
}

void SatelliteTrackerGUI::on_dateTimeSelect_currentIndexChanged(int index)
{
    m_settings.m_dateTimeSelect = (SatelliteTrackerSettings::DateTimeSelect)index;

    if (m_settings.m_dateTimeSelect != SatelliteTrackerSettings::CUSTOM)
    {
        m_settings.m_dateTime = "";
        ui->dateTime->setVisible(false);
    }
    else
    {
        m_settings.m_dateTime = ui->dateTime->dateTime().toString(Qt::ISODateWithMs);
        ui->dateTime->setVisible(true);
    }

    ui->deviceFeatureSelect->setVisible(m_settings.m_dateTimeSelect >= SatelliteTrackerSettings::FROM_MAP);
    updateDeviceFeatureCombo();

    m_settingsKeys.append("dateTimeSelect");
    m_settingsKeys.append("dateTime");

    applySettings();
    plotChart();
}

void SatelliteTrackerGUI::on_dateTime_dateTimeChanged(const QDateTime &datetime)
{
    (void) datetime;

    if (ui->dateTimeSelect->currentIndex() == 1)
    {
        m_settings.m_dateTime = ui->dateTime->dateTime().toString(Qt::ISODateWithMs);
        m_settingsKeys.append("dateTime");
        applySettings();
        plotChart();
    }
}

// Find target on the Map
void SatelliteTrackerGUI::on_viewOnMap_clicked()
{
    if (!m_settings.m_target.isEmpty()) {
        FeatureWebAPIUtils::mapFind(m_settings.m_target);
    }
}

void SatelliteTrackerGUI::on_updateSatData_clicked()
{
    m_satelliteTracker->getInputMessageQueue()->push(SatelliteTracker::MsgUpdateSatData::create());
}

void SatelliteTrackerGUI::on_selectSats_clicked()
{
    SatelliteSelectionDialog dialog(&m_settings, m_satellites);
    new DialogPositioner(&dialog, true);
    if (dialog.exec() == QDialog::Accepted)
    {
        updateSelectedSats();
        updateSatelliteGroups();
        m_settingsKeys.append("satellites");
        m_settingsKeys.append("hideStaleTLEs");
        m_settingsKeys.append("maxTLEAge");
        m_settingsKeys.append("satelliteGroups");
        m_settingsKeys.append("satelliteGroup");
        applySettings();
    }
}

void SatelliteTrackerGUI::on_radioControl_clicked()
{
    SatelliteRadioControlDialog dialog(&m_settings, m_satellites);
    new DialogPositioner(&dialog, true);
    if (dialog.exec() == QDialog::Accepted)
    {
        m_settingsKeys.append("deviceSettings");
        applySettings();
    }
}

void SatelliteTrackerGUI::on_autoTarget_clicked(bool checked)
{
    m_settings.m_autoTarget = checked;
    m_settingsKeys.append("autoTarget");
    applySettings();
}

void SatelliteTrackerGUI::updateStatus()
{
    // Indicate if satellite data is being updated
    bool updatingSatData = m_satelliteTracker->isUpdatingSatData();

    if (updatingSatData != m_lastUpdatingSatData)
    {
        if (updatingSatData) {
            ui->updateSatData->setStyleSheet("QToolButton { background-color : green; }");
        } else {
            ui->updateSatData->setStyleSheet("QToolButton { background: none; }");
        }

        m_lastUpdatingSatData = updatingSatData;
    }

    updateTimeToAOS();
    updateDeviceFeatureCombo();
}

void SatelliteTrackerGUI::updateFeatureState()
{
    updateStartStopButton(ui->startStop);

    // Worker, which does the Doppler tracking, is deleted when stopped
    if (m_satelliteTracker->getState() != Feature::StRunning) {
        stopTune();
    }
}

// Update time to AOS
void SatelliteTrackerGUI::updateTimeToAOS()
{
    if (m_geostationarySatVisible)
    {
        ui->aos->setText("Now");
    }
    else if (m_nextTargetAOS.isValid())
    {
        QDateTime currentTime = m_satelliteTracker->currentDateTime();            // FIXME: UTC
        int secondsToAOS = m_nextTargetAOS.toSecsSinceEpoch() - currentTime.toSecsSinceEpoch();

        if (secondsToAOS > 0)
        {
            int seconds = secondsToAOS % 60;
            int minutes = (secondsToAOS / 60) % 60;
            int hours = (secondsToAOS / (60 * 60)) % 24;
            int days = secondsToAOS / (60 * 60 * 24);

            if (days == 1)
            {
                ui->aos->setText(QString("1 day"));
            }
            else if (days > 0)
            {
                ui->aos->setText(QString("%1 days").arg(days));
            }
            else
            {
                ui->aos->setText(QString("%1:%2:%3")
                    .arg(hours, 2, 10, QLatin1Char('0'))
                    .arg(minutes, 2, 10, QLatin1Char('0'))
                    .arg(seconds, 2, 10, QLatin1Char('0')));
            }
        }
        else if (m_nextTargetLOS < currentTime)
        {
            ui->aos->setText("");
        }
        else
        {
            ui->aos->setText("Now");
        }
    }
    else
        ui->aos->setText("");
}

void SatelliteTrackerGUI::applySettings(bool force)
{
    if (m_doApplySettings)
    {
        SatelliteTracker::MsgConfigureSatelliteTracker* message = SatelliteTracker::MsgConfigureSatelliteTracker::create(
            m_settings, m_settingsKeys, force);
        m_satelliteTracker->getInputMessageQueue()->push(message);
    }

    m_settingsKeys.clear();
}

void SatelliteTrackerGUI::on_nextPass_clicked()
{
    if (m_targetSatState != nullptr)
    {
        if (m_plotPass < m_targetSatState->m_passes.size() - 1)
        {
            m_plotPass++;
            ui->passLabel->setText(QString("%1").arg(m_plotPass));
            plotChart();
        }
    }
}

void SatelliteTrackerGUI::on_prevPass_clicked()
{
    if (m_plotPass > 0)
    {
        m_plotPass--;
        ui->passLabel->setText(QString("%1").arg(m_plotPass));
        plotChart();
    }
}

void SatelliteTrackerGUI::on_darkTheme_clicked(bool checked)
{
    m_settings.m_chartsDarkTheme = checked;
    plotChart();
    m_settingsKeys.append("chartsDarkTheme");
    applySettings();
}

void SatelliteTrackerGUI::on_chartSelect_currentIndexChanged(int index)
{
    (void) index;
    plotChart();
}

void SatelliteTrackerGUI::plotChart()
{
    if (ui->chartSelect->currentIndex() == 0) {
        plotPolarChart();
    } else {
        plotAzElChart();
    }
}

// Linear interpolation
static double interpolate(double x0, double y0, double x1, double y1, double x)
{
    return (y0*(x1-x) + y1*(x-x0)) / (x1-x0);
}

// Reduce az/el range from 450,180 to 360,90
void SatelliteTrackerGUI::limitAzElRange(double& azimuth, double& elevation) const
{
    if (elevation > 90.0)
    {
        elevation = 180.0 - elevation;
        if (azimuth < 180.0) {
            azimuth += 180.0;
        } else {
            azimuth -= 180.0;
        }
    }
    if (azimuth > 360.0) {
        azimuth -= 360.0f;
    }
    if (azimuth == 0) {
        azimuth = 360.0;
    }
}

// Plot pass in polar coords
void SatelliteTrackerGUI::plotPolarChart()
{
    if ((m_targetSatState == nullptr) || !m_satellites.contains(m_settings.m_target) || (m_targetSatState->m_passes.size() == 0))
    {
        ui->passChart->setChart(&m_emptyChart);
        return;
    }

    QChart *oldChart = m_polarChart;

    if (m_plotPass >= m_targetSatState->m_passes.size() - 1)
    {
        m_plotPass = m_targetSatState->m_passes.size() - 1;
        ui->passLabel->setText(QString("%1").arg(m_plotPass));
    }

    const SatellitePass &pass = m_targetSatState->m_passes[m_plotPass];

    // Always create a new chart, otherwise sometimes they aren't drawn properly
    m_polarChart = new QPolarChart();
    m_polarChart->setTheme(m_settings.m_chartsDarkTheme ? QChart::ChartThemeDark : QChart::ChartThemeLight);
    QValueAxis *angularAxis = new QValueAxis();
    QCategoryAxis *radialAxis = new QCategoryAxis();

    angularAxis->setTickCount(9);
    angularAxis->setMinorTickCount(1);
    angularAxis->setLabelFormat("%d");
    angularAxis->setRange(0, 360);

    radialAxis->setMin(0);
    radialAxis->setMax(90);
    radialAxis->append("90", 0);
    radialAxis->append("60", 30);
    radialAxis->append("30", 60);
    radialAxis->append("0", 90);
    radialAxis->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);

    m_polarChart->addAxis(angularAxis, QPolarChart::PolarOrientationAngular);
    m_polarChart->addAxis(radialAxis, QPolarChart::PolarOrientationRadial);
    m_polarChart->legend()->hide();
    m_polarChart->layout()->setContentsMargins(0, 0, 0, 0);
    m_polarChart->setMargins(QMargins(1, 1, 1, 1));

    SatNogsSatellite *sat = m_satellites.value(m_settings.m_target);

    if (pass.m_aos.isValid() && pass.m_los.isValid())
    {
        QString title;

        if (m_settings.m_utc) {
            title = pass.m_aos.date().toString(m_settings.m_dateFormat);
        } else {
            title = pass.m_aos.toLocalTime().date().toString(m_settings.m_dateFormat);
        }

        m_polarChart->setTitle(QString("%1").arg(title));
        QLineSeries *polarSeries = new QLineSeries();

        getPassAzEl(
            nullptr,
            nullptr,
            polarSeries,
            *sat->m_tle,
            m_settings.m_latitude,
            m_settings.m_longitude,
            m_settings.m_heightAboveSeaLevel/1000.0,
            pass.m_aos, pass.m_los
        );

        // Polar charts can't handle points that are more than 180 degrees apart, so
        // we need to split passes that cross from 359 -> 0 degrees (or the reverse)
        QList<QLineSeries *> series;
        series.append(new QLineSeries());
        QLineSeries *s = series.first();
        QPen pen(QColor(32, 159, 223), 2, Qt::SolidLine);
        s->setPen(pen);

        qreal prevAz = polarSeries->at(0).x();
        qreal prevEl = polarSeries->at(0).y();

        for (int i = 1; i < polarSeries->count(); i++)
        {
            qreal az = polarSeries->at(i).x();
            qreal el = polarSeries->at(i).y();

            if ((prevAz > 270.0) && (az <= 90.0))
            {
                double elMid = interpolate(prevAz, prevEl, az+360.0, el, 360.0);
                s->append(360.0, elMid);
                series.append(new QLineSeries());
                s = series.last();
                s->setPen(pen);
                s->append(0.0, elMid);
                s->append(az, el);
            }
            else if ((prevAz <= 90.0) && (az > 270.0))
            {
                double elMid = interpolate(prevAz, prevEl, az-360.0, el, 0.0);
                s->append(0.0, elMid);
                series.append(new QLineSeries());
                s = series.last();
                s->setPen(pen);
                s->append(360.0, elMid);
                s->append(az, el);
            }
            else
            {
                s->append(polarSeries->at(i));
            }

            prevAz = az;
            prevEl = el;
        }

        for (int i = 0; i < series.length(); i++)
        {
            m_polarChart->addSeries(series[i]);
            series[i]->attachAxis(angularAxis);
            series[i]->attachAxis(radialAxis);
        }

        int redrawTime = 0;

        if (m_settings.m_drawRotators != SatelliteTrackerSettings::NO_ROTATORS)
        {
            // Plot rotator position
            QString ourSourceName = QString("F:%1 %2").arg(m_satelliteTracker->getIndexInFeatureSet()).arg(m_satelliteTracker->getIdentifier());
            std::vector<FeatureSet*>& featureSets = MainCore::instance()->getFeatureeSets();
            for (int featureSetIndex = 0; featureSetIndex < (int)featureSets.size(); featureSetIndex++)
            {
                FeatureSet *featureSet = featureSets[featureSetIndex];
                for (int featureIndex = 0; featureIndex < featureSet->getNumberOfFeatures(); featureIndex++)
                {
                    Feature *feature = featureSet->getFeatureAt(featureIndex);
                    if (FeatureUtils::compareFeatureURIs(feature->getURI(), "sdrangel.feature.gs232controller"))
                    {
                        QString source;
                        ChannelWebAPIUtils::getFeatureSetting(featureSetIndex, featureIndex, "source", source); // Will return false if source isn't set in Controller
                        int track = 0;
                        ChannelWebAPIUtils::getFeatureSetting(featureSetIndex, featureIndex, "track", track);
                        if ((m_settings.m_drawRotators == SatelliteTrackerSettings::ALL_ROTATORS) || ((source == ourSourceName) && track))
                        {
                            int onTarget = 0;
                            ChannelWebAPIUtils::getFeatureReportValue(featureSetIndex, featureIndex, "onTarget", onTarget);

                            if (!onTarget)
                            {
                                // Target azimuth red dotted line
                                double targetAzimuth, targetElevation;
                                bool targetAzimuthOk = ChannelWebAPIUtils::getFeatureReportValue(featureSetIndex, featureIndex, "targetAzimuth", targetAzimuth);
                                bool targetElevationOk = ChannelWebAPIUtils::getFeatureReportValue(featureSetIndex, featureIndex, "targetElevation", targetElevation);
                                if (targetAzimuthOk && targetElevationOk)
                                {
                                    limitAzElRange(targetAzimuth, targetElevation);

                                    QScatterSeries *rotatorSeries = new QScatterSeries();
                                    QColor color(255, 0, 0, 150);
                                    QPen pen(color);
                                    rotatorSeries->setPen(pen);
                                    rotatorSeries->setColor(color.darker());
                                    rotatorSeries->setMarkerSize(20);
                                    rotatorSeries->append(targetAzimuth, 90-targetElevation);
                                    m_polarChart->addSeries(rotatorSeries);
                                    rotatorSeries->attachAxis(angularAxis);
                                    rotatorSeries->attachAxis(radialAxis);

                                    redrawTime = 333;
                                }
                            }

                            // Current azimuth line. Yellow while off target, green on target.
                            double currentAzimuth, currentElevation;
                            bool currentAzimuthOk = ChannelWebAPIUtils::getFeatureReportValue(featureSetIndex, featureIndex, "currentAzimuth", currentAzimuth);
                            bool currentElevationOk = ChannelWebAPIUtils::getFeatureReportValue(featureSetIndex, featureIndex, "currentElevation", currentElevation);
                            if (currentAzimuthOk && currentElevationOk)
                            {
                                limitAzElRange(currentAzimuth, currentElevation);

                                QScatterSeries *rotatorSeries = new QScatterSeries();
                                QColor color;
                                if (onTarget) {
                                    color = QColor(0, 255, 0, 150);
                                } else {
                                    color = QColor(255, 255, 0, 150);
                                }
                                rotatorSeries->setPen(QPen(color));
                                rotatorSeries->setColor(color.darker());
                                rotatorSeries->setMarkerSize(20);
                                rotatorSeries->append(currentAzimuth, 90-currentElevation);
                                m_polarChart->addSeries(rotatorSeries);
                                rotatorSeries->attachAxis(angularAxis);
                                rotatorSeries->attachAxis(radialAxis);

                                redrawTime = 333;
                            }
                        }
                    }
                }
            }
        }

        // Create series with single point, so we can plot time of AOS
        QLineSeries *aosSeries = new QLineSeries();
        aosSeries->append(polarSeries->at(0));
        QTime time;

        if (m_settings.m_utc) {
            time = pass.m_aos.time();
        } else {
            time = pass.m_aos.toLocalTime().time();
        }

        if (m_settings.m_utc) {
            aosSeries->setPointLabelsFormat(QString("AOS %1").arg(time.toString("hh:mm")));
        } else {
            aosSeries->setPointLabelsFormat(QString("AOS %1").arg(time.toString("hh:mm")));
        }

        aosSeries->setPointLabelsVisible(true);
        aosSeries->setPointLabelsClipping(false);
        m_polarChart->addSeries(aosSeries);
        aosSeries->attachAxis(angularAxis);
        aosSeries->attachAxis(radialAxis);
        // Create series with single point, so we can plot time of LOS
        QLineSeries *losSeries = new QLineSeries();
        losSeries->append(polarSeries->at(polarSeries->count()-1));

        if (m_settings.m_utc) {
            time = pass.m_los.time();
        } else {
            time = pass.m_los.toLocalTime().time();
        }

        losSeries->setPointLabelsFormat(QString("LOS %1").arg(time.toString("hh:mm")));
        losSeries->setPointLabelsVisible(true);
        losSeries->setPointLabelsClipping(false);
        m_polarChart->addSeries(losSeries);
        losSeries->attachAxis(angularAxis);
        losSeries->attachAxis(radialAxis);
        QDateTime currentTime;

        if (m_settings.m_dateTime == "") {
            currentTime = m_satelliteTracker->currentDateTimeUtc();
        } else if (m_settings.m_utc) {
            currentTime = QDateTime::fromString(m_settings.m_dateTime, Qt::ISODateWithMs);
        } else {
            currentTime = QDateTime::fromString(m_settings.m_dateTime, Qt::ISODateWithMs).toUTC();
        }

        if ((currentTime >= pass.m_aos) && (currentTime <= pass.m_los))
        {
            // Create series with single point, so we can plot current time
            QScatterSeries *nowSeries = new QScatterSeries();
            nowSeries->setMarkerSize(3);
            // Find closest point to current time
            int idx = std::round(polarSeries->count() * (currentTime.toMSecsSinceEpoch() - pass.m_aos.toMSecsSinceEpoch())
                                                       / (pass.m_los.toMSecsSinceEpoch() - pass.m_aos.toMSecsSinceEpoch()));
            nowSeries->append(polarSeries->at(idx));
            nowSeries->setPointLabelsFormat(m_settings.m_target);
            nowSeries->setPointLabelsVisible(true);
            nowSeries->setPointLabelsClipping(false);
            m_polarChart->addSeries(nowSeries);
            nowSeries->attachAxis(angularAxis);
            nowSeries->attachAxis(radialAxis);
        }

        if (redrawTime > 0)
        {
            // Redraw to show updated rotator position
            m_redrawTimer.setSingleShot(true);
            m_redrawTimer.start(redrawTime);
        }

        delete polarSeries;
    }
    else
    {
        // Possibly geostationary, just plot current position
        QDateTime currentTime;

        if (m_settings.m_dateTime == "") {
            currentTime = m_satelliteTracker->currentDateTimeUtc();
        } else if (m_settings.m_utc) {
            currentTime = QDateTime::fromString(m_settings.m_dateTime, Qt::ISODateWithMs);
        } else {
            currentTime = QDateTime::fromString(m_settings.m_dateTime, Qt::ISODateWithMs).toUTC();
        }

        QString title;

        if (m_settings.m_utc) {
            title = currentTime.date().toString(m_settings.m_dateFormat);
        } else {
            title = currentTime.toLocalTime().date().toString(m_settings.m_dateFormat);
        }

        m_polarChart->setTitle(QString("%1").arg(title));
        QLineSeries *nowSeries = new QLineSeries();
        QDateTime endTime = currentTime.addSecs(1);

        getPassAzEl(
            nullptr,
            nullptr,
            nowSeries,
            *sat->m_tle,
            m_settings.m_latitude,
            m_settings.m_longitude,
            m_settings.m_heightAboveSeaLevel/1000.0,
            currentTime, endTime
        );

        nowSeries->setPointLabelsFormat(m_settings.m_target);
        nowSeries->setPointLabelsVisible(true);
        nowSeries->setPointLabelsClipping(false);
        m_polarChart->addSeries(nowSeries);
        nowSeries->attachAxis(angularAxis);
        nowSeries->attachAxis(radialAxis);
    }

    ui->passChart->setChart(m_polarChart);
    delete oldChart;
}

// Plot target elevation/azimuth for the next pass
void SatelliteTrackerGUI::plotAzElChart()
{
    if ((m_targetSatState == nullptr) || !m_satellites.contains(m_settings.m_target) || (m_targetSatState->m_passes.size() == 0))
    {
        ui->passChart->setChart(&m_emptyChart);
        return;
    }

    QChart *oldChart = m_lineChart;

    if (m_plotPass >= m_targetSatState->m_passes.size() - 1)
    {
        m_plotPass = m_targetSatState->m_passes.size() - 1;
        ui->passLabel->setText(QString("%1").arg(m_plotPass));
    }

    const SatellitePass &pass = m_targetSatState->m_passes[m_plotPass];
    // Always create a new chart, otherwise sometimes they aren't drawn properly
    m_lineChart = new QChart();
    m_lineChart->setTheme(m_settings.m_chartsDarkTheme ? QChart::ChartThemeDark : QChart::ChartThemeLight);
    QDateTimeAxis *xAxis = new QDateTimeAxis();
    QValueAxis *yLeftAxis = new QValueAxis();
    QValueAxis *yRightAxis = new QValueAxis();
    QString title;

    if (m_settings.m_utc) {
        title = pass.m_aos.date().toString(m_settings.m_dateFormat);
    } else {
        title = pass.m_aos.toLocalTime().date().toString(m_settings.m_dateFormat);
    }

    m_lineChart->setTitle(QString("%1").arg(title));
    m_lineChart->legend()->hide();
    m_lineChart->addAxis(xAxis, Qt::AlignBottom);
    m_lineChart->addAxis(yLeftAxis, Qt::AlignLeft);
    m_lineChart->addAxis(yRightAxis, Qt::AlignRight);
    m_lineChart->layout()->setContentsMargins(0, 0, 0, 0);
    m_lineChart->setMargins(QMargins(1, 1, 1, 1));
    SatNogsSatellite *sat = m_satellites.value(m_settings.m_target);
    QLineSeries *azSeries = new QLineSeries();
    QLineSeries *elSeries = new QLineSeries();

    getPassAzEl(
        azSeries,
        elSeries,
        nullptr,
        *sat->m_tle,
        m_settings.m_latitude,
        m_settings.m_longitude,
        m_settings.m_heightAboveSeaLevel/1000.0,
        pass.m_aos,
        pass.m_los
    );

    // Split crossing of 360/0 degrees in to multiple series in the same colour
    QList<QLineSeries *> azSeriesList;
    QPen pen(QColor(153, 202, 83), 2, Qt::SolidLine);
    QLineSeries *s = new QLineSeries();
    azSeriesList.append(s);
    s->setPen(pen);
    qreal prevAz = azSeries->at(0).y();

    for (int i = 0; i < azSeries->count(); i++)
    {
        qreal az = azSeries->at(i).y();

        if (((prevAz >= 270) && (az < 90)) || ((prevAz < 90) && (az >= 270)))
        {
            s = new QLineSeries();
            azSeriesList.append(s);
            s->setPen(pen);
        }

        s->append(azSeries->at(i).x(), az);
        prevAz = az;
    }

    m_lineChart->addSeries(elSeries);
    elSeries->attachAxis(xAxis);
    elSeries->attachAxis(yLeftAxis);

    for (int i = 0; i < azSeriesList.size(); i++)
    {
        m_lineChart->addSeries(azSeriesList[i]);
        azSeriesList[i]->attachAxis(xAxis);
        azSeriesList[i]->attachAxis(yRightAxis);
    }

    // Plot current target on elevation series
    if (m_targetSatState && (m_targetSatState->m_elevation > 0.0))
    {
        QDateTime currentTime;

        if (m_settings.m_dateTime == "") {
            currentTime = m_satelliteTracker->currentDateTimeUtc();
        } else if (m_settings.m_utc) {
            currentTime = QDateTime::fromString(m_settings.m_dateTime, Qt::ISODateWithMs);
        } else {
            currentTime = QDateTime::fromString(m_settings.m_dateTime, Qt::ISODateWithMs).toUTC();
        }

        QScatterSeries *posSeries = new QScatterSeries();
        posSeries->setMarkerSize(3);
        posSeries->append(currentTime.toMSecsSinceEpoch(), m_targetSatState->m_elevation);
        posSeries->setPointLabelsVisible(true);
        posSeries->setPointLabelsFormat(m_settings.m_target);
        posSeries->setPointLabelsClipping(false);
        m_lineChart->addSeries(posSeries);
        posSeries->attachAxis(xAxis);
        posSeries->attachAxis(yLeftAxis);
    }

    xAxis->setRange(pass.m_aos, pass.m_los);
    xAxis->setFormat("hh:mm");
    yLeftAxis->setRange(0.0, 90.0);
    yLeftAxis->setTickCount(7);
    yLeftAxis->setLabelFormat("%d");
    yLeftAxis->setTitleText(QString("Elevation (%1)").arg(QChar(0xb0)));
    yRightAxis->setRange(0.0, 360.0);
    yRightAxis->setTickCount(7);
    yRightAxis->setLabelFormat("%d");
    yRightAxis->setTitleText(QString("Azimuth (%1)").arg(QChar(0xb0)));

    ui->passChart->setChart(m_lineChart);

    delete azSeries;
    delete oldChart;
}

void SatelliteTrackerGUI::resizeTable()
{
    // Fill table with a row of dummy data that will size the columns nicely
    int row = ui->satTable->rowCount();
    ui->satTable->setRowCount(row + 1);
    ui->satTable->setItem(row, SAT_COL_NAME, new QTableWidgetItem("Satellite123"));
    ui->satTable->setItem(row, SAT_COL_AZ, new QTableWidgetItem("360"));
    ui->satTable->setItem(row, SAT_COL_EL, new QTableWidgetItem("-90"));
    ui->satTable->setItem(row, SAT_COL_TNE, new QTableWidgetItem("99:99:99 AOS"));
    ui->satTable->setItem(row, SAT_COL_DUR, new QTableWidgetItem("9:99:99"));
    ui->satTable->setItem(row, SAT_COL_AOS, new QTableWidgetItem("+1 10:17"));
    ui->satTable->setItem(row, SAT_COL_LOS, new QTableWidgetItem("+1 10:17"));
    ui->satTable->setItem(row, SAT_COL_MAX_EL, new QTableWidgetItem("90"));
    ui->satTable->setItem(row, SAT_COL_DIR, new QTableWidgetItem("^"));
    ui->satTable->setItem(row, SAT_COL_LATITUDE, new QTableWidgetItem("-90.0"));
    ui->satTable->setItem(row, SAT_COL_LONGITUDE, new QTableWidgetItem("-180.0"));
    ui->satTable->setItem(row, SAT_COL_ALT, new QTableWidgetItem("50000"));
    ui->satTable->setItem(row, SAT_COL_RANGE, new QTableWidgetItem("50000"));
    ui->satTable->setItem(row, SAT_COL_RANGE_RATE, new QTableWidgetItem("10.0"));
    ui->satTable->setItem(row, SAT_COL_DOPPLER, new QTableWidgetItem("10000"));
    ui->satTable->setItem(row, SAT_COL_PATH_LOSS, new QTableWidgetItem("100"));
    ui->satTable->setItem(row, SAT_COL_NORAD_ID, new QTableWidgetItem("123456"));
    ui->satTable->resizeColumnsToContents();
    ui->satTable->setRowCount(row);
}

// As we only have limited space in table, display time plus number of days to AOS/LOS
// unless it's greater than 10 days, in which case just display the date
QString SatelliteTrackerGUI::formatDaysTime(qint64 days, QDateTime dateTime)
{
    QDateTime dt;

    if (m_settings.m_utc) {
        dt = dateTime.toUTC();
    } else {
        dt = dateTime.toLocalTime();
    }

    if (abs(days) > 10) {
        return dt.date().toString(m_settings.m_dateFormat);
    } else if (days == 0) {
        return dt.time().toString("hh:mm");
    } else if (days > 0) {
        return dt.time().toString(QString("hh:mm +%1").arg(days));
    } else {
        return dt.time().toString(QString("hh:mm %1").arg(days));
    }
}

QString SatelliteTrackerGUI::formatSecondsAsHHMMSS(qint64 seconds)
{
    char const* sign = "";

    if (seconds < 0)
    {
        sign    = "-";
        seconds = -seconds;
    }

    int minutes = seconds / 60;
    seconds = seconds % 60;
    int hours = minutes / 60;
    minutes = minutes % 60;

    if (hours > 0) {
        return QString("%1%2:%3:%4").arg(sign).arg(hours).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
    } else {
        return QString("%1%2:%3").arg(sign).arg(minutes).arg(seconds, 2, 10, QChar('0'));
    }
}

// Table item showing some text, but sorted by datetime set as user data
class DateTimeSortedTableWidgetItem : public QTableWidgetItem {
public:
    bool operator<(const QTableWidgetItem& other) const
    {
        QVariant v1 = data(Qt::UserRole);
        QVariant v2 = other.data(Qt::UserRole);

        if (v1.isValid() && v2.isValid()) {
            return v1.toDateTime() < v2.toDateTime();
        } else {
            return false;
        }
    }
};

// Handle sorting for next column, which can have times as HH:MM:SS or MM:SS
class NextEventTableWidgetItem : public QTableWidgetItem
{
public:
    bool operator<(const QTableWidgetItem &other) const override
    {
        QString t1 = text();
        QString t2 = other.text();
        int t1Colons = t1.count(":");
        int t2Colons = t2.count(":");

        if (t1Colons == t2Colons)
        {
            QCollator coll;
            coll.setNumericMode(true);
            return coll.compare(t1, t2) < 0;
        }
        else
        {
            return t1Colons < t2Colons;
        }
    }
};

class NaturallySortedTableWidgetItem : public QTableWidgetItem
{
public:
    bool operator<(const QTableWidgetItem &other) const override
    {
        QCollator coll;
        coll.setNumericMode(true);
        return coll.compare(text() , other.text()) < 0;
    }
};

#define SPEED_OF_LIGHT 299792458.0

// Frequency in Hz, speed in m/s
static double doppler(double frequency, double speed)
{
    return frequency * speed / SPEED_OF_LIGHT;
}

// Frequency in Hz, speed in m/s
static double freeSpaceLoss(double frequency, double distance)
{
    return 20.0 * log10(distance) + 20 * log10(frequency) + 20 * log10(4*M_PI/SPEED_OF_LIGHT);
}

// Distance in m, delay in s
static double propagationDelay(double distance)
{
    return distance / SPEED_OF_LIGHT;
}

// Update satellite data table with latest data for the satellite
void SatelliteTrackerGUI::updateTable(SatelliteState *satState)
{
    // Does the table already contain this satellite?
    QList<QTableWidgetItem *> matches = ui->satTable->findItems(satState->m_name, Qt::MatchExactly);
    QTableWidgetItem *items[SAT_COL_COLUMNS];

    if (matches.size() == 0)
    {
        // Add a new row
        ui->satTable->setSortingEnabled(false);
        int row = ui->satTable->rowCount();
        ui->satTable->setRowCount(row + 1);

        for (int i = 0; i < SAT_COL_COLUMNS; i++)
        {
            if ((i == SAT_COL_AOS) || (i == SAT_COL_LOS)) {
                items[i] = new DateTimeSortedTableWidgetItem();
            } else if ((i == SAT_COL_NAME) || (i == SAT_COL_NORAD_ID)) {
                items[i] = new QTableWidgetItem();
            } else if (i == SAT_COL_TNE) {
                items[i] = new NextEventTableWidgetItem();
            } else {
                items[i] = new NaturallySortedTableWidgetItem();
            }

            items[i]->setToolTip(ui->satTable->horizontalHeaderItem(i)->toolTip());
            ui->satTable->setItem(row, i, items[i]);
        }

        ui->satTable->setSortingEnabled(true);
        // Static columns
        items[SAT_COL_NAME]->setText(satState->m_name);

        if (m_satellites.contains(satState->m_name))
        {
            SatNogsSatellite *sat = m_satellites.value(satState->m_name);
            items[SAT_COL_NORAD_ID]->setData(Qt::DisplayRole, sat->m_noradCatId);
        }

        // Text alignment
        for (int col : {SAT_COL_AZ, SAT_COL_EL, SAT_COL_TNE, SAT_COL_DUR, SAT_COL_MAX_EL,
            SAT_COL_LATITUDE, SAT_COL_LONGITUDE,
            SAT_COL_ALT, SAT_COL_RANGE, SAT_COL_RANGE_RATE, SAT_COL_DOPPLER,
            SAT_COL_PATH_LOSS, SAT_COL_DELAY})
        {
            items[col]->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
        }
    }
    else
    {
        // Update existing row
        int row = ui->satTable->row(matches[0]);

        for (int i = 0; i < SAT_COL_COLUMNS; i++) {
            items[i] = ui->satTable->item(row, i);
        }
    }

    items[SAT_COL_AZ]->setData(Qt::DisplayRole, (int)round(satState->m_azimuth));
    items[SAT_COL_EL]->setData(Qt::DisplayRole, (int)round(satState->m_elevation));

    if (satState->m_passes.size() > 0)
    {
        // Get number of days to AOS/LOS
        QDateTime currentDateTime = m_satelliteTracker->currentDateTime();
        int daysToAOS = currentDateTime.daysTo(satState->m_passes[0].m_aos);
        int daysToLOS = currentDateTime.daysTo(satState->m_passes[0].m_los);

        if (satState->m_passes[0].m_aos > currentDateTime) {
            items[SAT_COL_TNE]->setText(formatSecondsAsHHMMSS(currentDateTime.secsTo(satState->m_passes[0].m_aos))+" AOS");
        } else {
            items[SAT_COL_TNE]->setText(formatSecondsAsHHMMSS(currentDateTime.secsTo(satState->m_passes[0].m_los))+" LOS");
        }

        items[SAT_COL_DUR]->setText(formatSecondsAsHHMMSS(satState->m_passes[0].m_aos.secsTo(satState->m_passes[0].m_los)));
        items[SAT_COL_AOS]->setText(formatDaysTime(daysToAOS, satState->m_passes[0].m_aos));
        items[SAT_COL_AOS]->setData(Qt::UserRole, satState->m_passes[0].m_aos);
        items[SAT_COL_LOS]->setText(formatDaysTime(daysToLOS, satState->m_passes[0].m_los));
        items[SAT_COL_LOS]->setData(Qt::UserRole, satState->m_passes[0].m_los);
        items[SAT_COL_MAX_EL]->setData(Qt::DisplayRole, (int)round(satState->m_passes[0].m_maxElevation));

        if (satState->m_passes[0].m_northToSouth) {
            items[SAT_COL_DIR]->setText(QString("%1").arg(QChar(0x2193))); // Down arrow
        } else {
            items[SAT_COL_DIR]->setText(QString("%1").arg(QChar(0x2191))); // Up arrow
        }
    }
    else
    {
        items[SAT_COL_TNE]->setText("");
        items[SAT_COL_DUR]->setText("");
        items[SAT_COL_AOS]->setText("");
        items[SAT_COL_LOS]->setText("");
        items[SAT_COL_MAX_EL]->setData(Qt::DisplayRole, QVariant());
        items[SAT_COL_DIR]->setText("");
    }

    items[SAT_COL_LATITUDE]->setData(Qt::DisplayRole, satState->m_latitude);
    items[SAT_COL_LONGITUDE]->setData(Qt::DisplayRole, satState->m_longitude);
    items[SAT_COL_ALT]->setData(Qt::DisplayRole, (int)round(satState->m_altitude));
    items[SAT_COL_RANGE]->setData(Qt::DisplayRole, (int)round(satState->m_range));
    items[SAT_COL_RANGE_RATE]->setData(Qt::DisplayRole, QString::number(satState->m_rangeRate, 'f', 3));
    items[SAT_COL_DOPPLER]->setData(Qt::DisplayRole, (int)round(-doppler(m_settings.m_defaultFrequency, satState->m_rangeRate*1000.0)));
    items[SAT_COL_PATH_LOSS]->setData(Qt::DisplayRole, QString::number(freeSpaceLoss(m_settings.m_defaultFrequency, satState->m_range*1000.0), 'f', 1));
    items[SAT_COL_DELAY]->setData(Qt::DisplayRole, QString::number(propagationDelay(satState->m_range*1000.0)*1000.0, 'f', 1));
}

void SatelliteTrackerGUI::on_satTable_cellDoubleClicked(int row, int column)
{
    (void) column;

    QString sat = ui->satTable->item(row, SAT_COL_NAME)->text();
    FeatureWebAPIUtils::mapFind(sat);
}

void SatelliteTrackerGUI::satTable_customContextMenuRequested(QPoint pos)
{
    QTableWidgetItem *item = ui->satTable->itemAt(pos);
    if (!item) {
        return;
    }

    QString name = ui->satTable->item(item->row(), SAT_COL_NAME)->text();
    SatNogsSatellite *sat = m_satellites.value(name);

    QMenu *tableContextMenu = new QMenu(ui->satTable);
    connect(tableContextMenu, &QMenu::aboutToHide, tableContextMenu, &QMenu::deleteLater);

    if (!m_tuneSatellite.isEmpty())
    {
        QAction *stopAction = new QAction(QString("Stop Doppler tracking %1 (%2)").arg(m_tuneSatellite).arg(SatNogsTransmitter::getFrequencyText(m_tuneFrequency + m_tuneOffset)), tableContextMenu);
        connect(stopAction, &QAction::triggered, this, &SatelliteTrackerGUI::stopTune);
        tableContextMenu->addAction(stopAction);
        tableContextMenu->addSeparator();
    }

    // Get list of Rx channels that can be tuned
    QList<RxChannel> rxChannels;
    QList<int> rxDeviceSets;
    std::vector<DeviceSet*>& deviceSets = MainCore::instance()->getDeviceSets();
    for (int i = 0; i < (int) deviceSets.size(); i++)
    {
        // Single Rx device sets, or Rx streams of MIMO device sets
        DeviceSet *deviceSet = deviceSets[i];
        if (!deviceSet->m_deviceSourceEngine && !deviceSet->m_deviceMIMOEngine) {
            continue;
        }
        rxDeviceSets.append(i);
        for (int j = 0; j < deviceSet->getNumberOfChannels(); j++)
        {
            ChannelAPI *channel = deviceSet->getChannelAt(j);
            if (channel && (channel->getStreamType() == ChannelAPI::StreamSingleSink))
            {
                QString label = deviceSet->m_deviceMIMOEngine
                    ? QString("M%1:%2 %3 (Rx%4)").arg(i).arg(j).arg(channel->getIdentifier()).arg(channel->getStreamIndex())
                    : QString("R%1:%2 %3").arg(i).arg(j).arg(channel->getIdentifier());
                if (channel == m_createdChannel) {
                    label.append(" [tracker]");
                }
                rxChannels.append({i, j, channel->getIdentifier(), label, channel});
            }
        }
    }

    // Add transmitters with a downlink frequency
    QList<SatNogsTransmitter *> transmitters;
    if (sat)
    {
        for (SatNogsTransmitter *transmitter : sat->m_transmitters)
        {
            if (transmitter->m_alive && (transmitter->m_downlinkLow > 0)) {
                transmitters.append(transmitter);
            }
        }
    }
    std::sort(transmitters.begin(), transmitters.end(), [](const SatNogsTransmitter *a, const SatNogsTransmitter *b) {
        return a->m_downlinkLow < b->m_downlinkLow;
    });

    // Channel to tune, as selected in Rx channel submenu, defaulting to first available
    int selectedChannel = rxChannels.isEmpty() ? -1 : 0;
    for (int i = 0; i < rxChannels.size(); i++)
    {
        if (QString("%1:%2").arg(rxChannels[i].m_deviceSetIndex).arg(rxChannels[i].m_channelIndex) == m_settings.m_tuneChannel) {
            selectedChannel = i;
        }
    }

    if (m_satelliteTracker->getState() != Feature::StRunning)
    {
        QAction *notRunningAction = new QAction("Start Satellite Tracker to tune to a transmitter", tableContextMenu);
        notRunningAction->setEnabled(false);
        tableContextMenu->addAction(notRunningAction);
    }
    else if (transmitters.isEmpty())
    {
        QAction *noneAction = new QAction(QString("No known downlink frequencies for %1").arg(name), tableContextMenu);
        noneAction->setEnabled(false);
        tableContextMenu->addAction(noneAction);
    }
    else
    {
        // Selecting a transmitter tunes a channel suited to its mode, or the selected Rx channel
        for (const SatNogsTransmitter *transmitter : transmitters)
        {
            // For transponders, tune to centre of passband
            qint64 frequency = transmitter->m_downlinkLow;
            QString frequencyText = SatNogsTransmitter::getFrequencyText(frequency);
            if (transmitter->m_downlinkHigh > transmitter->m_downlinkLow)
            {
                frequency = (transmitter->m_downlinkLow + transmitter->m_downlinkHigh) / 2;
                frequencyText = SatNogsTransmitter::getFrequencyRangeText(transmitter->m_downlinkLow, transmitter->m_downlinkHigh);
            }
            TuneMode mode = tuneMode(transmitter);
            int channel = channelForMode(mode, rxChannels, selectedChannel);

            // Create a suitable demodulator if there isn't one, on the selected channel's device
            QString newChannelType;
            int newChannelDeviceSetIndex = selectedChannel >= 0 ? rxChannels[selectedChannel].m_deviceSetIndex : (rxDeviceSets.isEmpty() ? -1 : rxDeviceSets[0]);
            QStringList types = channelTypesForMode(mode);
            if ((newChannelDeviceSetIndex >= 0) && !types.isEmpty() && ((channel < 0) || !types.contains(rxChannels[channel].m_type))) {
                newChannelType = availableChannelType(types);
            }

            QString text = QString("%1 %2 - %3").arg(frequencyText).arg(transmitter->m_mode).arg(transmitter->m_description);
            if (transmitter->m_status != "active") {
                text.append(QString(" (%1)").arg(transmitter->m_status));
            }
            float ctcss = transmitter->m_ctcss;
            if (ctcss > 0.0f) {
                text.append(QString(" [uplink CTCSS %1 Hz]").arg(ctcss, 0, 'f', 1));
            }
            if (!newChannelType.isEmpty())
            {
                QChar deviceType = deviceSets[newChannelDeviceSetIndex]->m_deviceMIMOEngine ? 'M' : 'R';
                text.append(QString("  →  new %1 on %2%3").arg(newChannelType).arg(deviceType).arg(newChannelDeviceSetIndex));
            }
            else if (channel >= 0)
            {
                text.append(QString("  →  %1").arg(rxChannels[channel].m_label));
            }

            QAction *tuneAction = new QAction(text, tableContextMenu);
            tuneAction->setCheckable(true);
            tuneAction->setChecked((m_tuneSatellite == name) && (m_tuneFrequency == frequency));
            tuneAction->setEnabled(!newChannelType.isEmpty() || (channel >= 0));
            if (!newChannelType.isEmpty())
            {
                connect(tuneAction, &QAction::triggered, this, [this, name, frequency, mode, ctcss, newChannelDeviceSetIndex, newChannelType]() {
                    m_tuneCTCSS = ctcss;
                    workSatellite(name);
                    tuneNewChannel(name, frequency, mode, newChannelDeviceSetIndex, newChannelType);
                });
            }
            else if (channel >= 0)
            {
                RxChannel rxChannel = rxChannels[channel];
                connect(tuneAction, &QAction::triggered, this, [this, name, frequency, mode, ctcss, rxChannel]() {
                    m_tuneCTCSS = ctcss;
                    workSatellite(name);
                    tuneChannel(name, frequency, mode, rxChannel);
                });
            }
            tableContextMenu->addAction(tuneAction);
        }
    }

    // Submenu to select which Rx channel to tune
    tableContextMenu->addSeparator();
    QMenu *channelMenu = tableContextMenu->addMenu(selectedChannel >= 0
        ? QString("Rx channel: %1").arg(rxChannels[selectedChannel].m_label)
        : QString("Rx channel: none available"));
    channelMenu->setEnabled(!rxChannels.isEmpty());
    QActionGroup *channelGroup = new QActionGroup(channelMenu);
    for (int i = 0; i < rxChannels.size(); i++)
    {
        QAction *channelAction = new QAction(rxChannels[i].m_label, channelGroup);
        channelAction->setCheckable(true);
        channelAction->setChecked(i == selectedChannel);
        RxChannel rxChannel = rxChannels[i];
        connect(channelAction, &QAction::triggered, this, [this, rxChannel]() {
            m_settings.m_tuneChannel = QString("%1:%2").arg(rxChannel.m_deviceSetIndex).arg(rxChannel.m_channelIndex);
            m_settingsKeys.append("tuneChannel");
            applySettings();
            // Move Doppler tracking to the newly selected channel
            if (!m_tuneSatellite.isEmpty() && (rxChannel.m_channel != m_tuneChannel)) {
                tuneChannel(m_tuneSatellite, m_tuneFrequency, m_tuneMode, rxChannel, m_tuneOffset);
            }
        });
        channelMenu->addAction(channelAction);
    }

    tableContextMenu->popup(ui->satTable->viewport()->mapToGlobal(pos));
}

// Classify a SatNOGS transmitter by how it should be received
SatelliteTrackerGUI::TuneMode SatelliteTrackerGUI::tuneMode(const SatNogsTransmitter *transmitter)
{
    QString m = transmitter->m_mode.trimmed().toUpper();
    QString description = transmitter->m_description.toUpper();

    if (m.contains("AFSK")) {
        return TUNE_AFSK;
    } else if ((m.startsWith("FSK") || m.startsWith("GMSK"))
        && (transmitter->m_baud == 9600)
        && (description.contains("G3RUH") || description.contains("AX.25") || description.contains("AX25"))) {
        return TUNE_G3RUH;
    } else if (m.startsWith("FM") || (m == "SSTV")) {
        return TUNE_FM;
    } else if ((m == "USB") || (m == "SSB") || ((m == "LSB") && transmitter->m_invert)) {
        // By convention, inverting transponders are received on USB (and transmitted to on LSB)
        return TUNE_USB;
    } else if (m == "LSB") {
        return TUNE_LSB;
    } else if (m.startsWith("CW")) {
        return TUNE_CW;
    } else if (m == "LORA") {
        return TUNE_LORA;
    } else if (m == "APT") {
        return TUNE_APT;
    } else if (m.startsWith("DVB-S")) {
        return TUNE_DVBS;
    } else {
        return TUNE_OTHER;
    }
}

// Channel types (in order of preference) suitable for a tune mode
QStringList SatelliteTrackerGUI::channelTypesForMode(TuneMode mode)
{
    switch (mode)
    {
    case TUNE_AFSK:
        return {"PacketDemod", "NFMDemod"};
    case TUNE_G3RUH:
        return {"PacketDemod"};
    case TUNE_FM:
        return {"NFMDemod"};
    case TUNE_USB:
    case TUNE_LSB:
    case TUNE_CW:
        return {"SSBDemod", "WDSPRx"};
    case TUNE_LORA:
        return {"ChirpChatDemod", "MeshtasticDemod"};
    case TUNE_APT:
        return {"APTDemod"};
    case TUNE_DVBS:
        return {"DATVDemod"};
    default:
        return {};
    }
}

// Pick channel to tune for a mode: the selected channel if it suits the mode, otherwise the
// first suitable channel (preferring the selected channel's device set), otherwise the selected channel
int SatelliteTrackerGUI::channelForMode(TuneMode mode, const QList<RxChannel>& rxChannels, int selectedChannel)
{
    if (selectedChannel < 0) {
        return -1;
    }

    const QStringList types = channelTypesForMode(mode);

    if (types.isEmpty() || types.contains(rxChannels[selectedChannel].m_type)) {
        return selectedChannel;
    }

    int deviceSetIndex = rxChannels[selectedChannel].m_deviceSetIndex;
    for (const QString& type : types)
    {
        int otherDeviceSet = -1;
        for (int i = 0; i < rxChannels.size(); i++)
        {
            if (rxChannels[i].m_type == type)
            {
                if (rxChannels[i].m_deviceSetIndex == deviceSetIndex) {
                    return i;
                } else if (otherDeviceSet < 0) {
                    otherDeviceSet = i;
                }
            }
        }
        if (otherDeviceSet >= 0) {
            return otherDeviceSet;
        }
    }

    return selectedChannel;
}

// Set demodulator settings to suit the mode
void SatelliteTrackerGUI::configureChannel(const RxChannel& rxChannel, TuneMode mode)
{
    int deviceSetIndex = rxChannel.m_deviceSetIndex;
    int channelIndex = rxChannel.m_channelIndex;

    if ((rxChannel.m_type == "SSBDemod") && ((mode == TUNE_USB) || (mode == TUNE_LSB) || (mode == TUNE_CW)))
    {
        // rfBandwidth and lowCutoff are negative for LSB
        double rfBandwidth = (mode == TUNE_CW) ? 1100.0 : 3000.0;
        double lowCutoff = 300.0;

        if (mode == TUNE_LSB)
        {
            rfBandwidth = -rfBandwidth;
            lowCutoff = -lowCutoff;
        }

        ChannelWebAPIUtils::patchChannelSetting(deviceSetIndex, channelIndex, "dsb", 0);
        ChannelWebAPIUtils::patchChannelSetting(deviceSetIndex, channelIndex, "rfBandwidth", rfBandwidth);
        ChannelWebAPIUtils::patchChannelSetting(deviceSetIndex, channelIndex, "lowCutoff", lowCutoff);
    }
    else if ((rxChannel.m_type == "PacketDemod") && ((mode == TUNE_AFSK) || (mode == TUNE_G3RUH)))
    {
        // Mode 0 is 1200 AFSK, 1 is 9600 G3RUH. Bandwidth isn't changed by setting mode via API
        bool g3ruh = mode == TUNE_G3RUH;

        ChannelWebAPIUtils::patchChannelSetting(deviceSetIndex, channelIndex, "mode", g3ruh ? 1 : 0);
        ChannelWebAPIUtils::patchChannelSetting(deviceSetIndex, channelIndex, "rfBandwidth", g3ruh ? 20000.0 : 12500.0);
    }
}

// When a transmitter is selected from the table, make the satellite the target, and have rotators track it
void SatelliteTrackerGUI::workSatellite(const QString& satellite)
{
    setTarget(satellite);
    startRotators();
}

// Enable tracking on and start rotator controllers that use this Satellite Tracker as their source
// If none do, and there's just one rotator controller that has no source, set its source to this Satellite Tracker
void SatelliteTrackerGUI::startRotators()
{
    std::vector<FeatureSet*>& featureSets = MainCore::instance()->getFeatureeSets();
    QString source;
    struct Rotator {
        int m_featureSetIndex;
        int m_featureIndex;
        Feature *m_feature;
        QString m_source;
    };
    QList<Rotator> rotators;

    for (int i = 0; i < (int) featureSets.size(); i++)
    {
        FeatureSet *featureSet = featureSets[i];
        for (int j = 0; j < featureSet->getNumberOfFeatures(); j++)
        {
            Feature *feature = featureSet->getFeatureAt(j);
            if (feature == m_satelliteTracker)
            {
                // Same format as AvailableChannelOrFeature::getLongId()
                source = QString("F:%1 %2").arg(j).arg(feature->getIdentifier());
            }
            else if (feature->getURI() == "sdrangel.feature.gs232controller")
            {
                QString rotatorSource;
                ChannelWebAPIUtils::getFeatureSetting(i, j, "source", rotatorSource);
                rotators.append({i, j, feature, rotatorSource});
            }
        }
    }

    if (source.isEmpty()) {
        return;
    }

    bool found = false;
    for (const Rotator& rotator : rotators) {
        found = found || (rotator.m_source == source);
    }
    if (!found && (rotators.size() == 1) && rotators[0].m_source.isEmpty())
    {
        ChannelWebAPIUtils::patchFeatureSetting(rotators[0].m_featureSetIndex, rotators[0].m_featureIndex, "source", source);
        rotators[0].m_source = source;
    }

    for (const Rotator& rotator : rotators)
    {
        if (rotator.m_source == source)
        {
            ChannelWebAPIUtils::patchFeatureSetting(rotator.m_featureSetIndex, rotator.m_featureIndex, "track", 1.0);
            if (rotator.m_feature->getState() != Feature::StRunning) {
                ChannelWebAPIUtils::runFeature(rotator.m_featureSetIndex, rotator.m_featureIndex);
            }
        }
    }
}

// First of the channel types that has a plugin available
QString SatelliteTrackerGUI::availableChannelType(const QStringList& types)
{
    PluginAPI::ChannelRegistrations *channelRegistrations = MainCore::instance()->getPluginManager()->getRxChannelRegistrations();

    for (const QString& type : types)
    {
        for (const PluginAPI::ChannelRegistration& registration : *channelRegistrations)
        {
            if (registration.m_channelId == type) {
                return type;
            }
        }
    }
    return "";
}

// Current index of a channel in a device set, or -1 if it no longer exists
// Indices change when channels are removed, so we keep pointers to channels
int SatelliteTrackerGUI::channelIndex(int deviceSetIndex, const ChannelAPI *channel)
{
    std::vector<DeviceSet*>& deviceSets = MainCore::instance()->getDeviceSets();

    if (channel && (deviceSetIndex >= 0) && (deviceSetIndex < (int) deviceSets.size()))
    {
        DeviceSet *deviceSet = deviceSets[deviceSetIndex];
        for (int i = 0; i < deviceSet->getNumberOfChannels(); i++)
        {
            if (deviceSet->getChannelAt(i) == channel) {
                return i;
            }
        }
    }
    return -1;
}

void SatelliteTrackerGUI::tuneChannel(const QString& satellite, qint64 frequency, TuneMode mode, const RxChannel& rxChannel, qint64 offset)
{
    m_pendingTune.m_active = false;

    if (m_createdChannel && (m_createdChannel != rxChannel.m_channel))
    {
        // Switching away from channel we created, so remove it. Stop tracking first, as channel indices will change,
        // and tune once it's been removed
        m_satelliteTracker->getInputMessageQueue()->push(SatelliteTracker::MsgTuneChannel::createStop());
        removeCreatedChannel();
        QTimer::singleShot(0, this, [this, satellite, frequency, mode, rxChannel, offset]() {
            startTune(satellite, frequency, mode, rxChannel.m_deviceSetIndex, rxChannel.m_channel, rxChannel.m_type, true, offset);
        });
    }
    else
    {
        startTune(satellite, frequency, mode, rxChannel.m_deviceSetIndex, rxChannel.m_channel, rxChannel.m_type, true, offset);
    }
}

// Add a new demodulator, and tune it once it's been created (in channelAdded)
void SatelliteTrackerGUI::tuneNewChannel(const QString& satellite, qint64 frequency, TuneMode mode, int deviceSetIndex, const QString& type)
{
    PluginAPI::ChannelRegistrations *channelRegistrations = MainCore::instance()->getPluginManager()->getRxChannelRegistrations();
    QString uri;

    for (const PluginAPI::ChannelRegistration& registration : *channelRegistrations)
    {
        if (registration.m_channelId == type) {
            uri = registration.m_channelIdURI;
        }
    }

    if (!m_tuneSatellite.isEmpty())
    {
        m_satelliteTracker->getInputMessageQueue()->push(SatelliteTracker::MsgTuneChannel::createStop());
        m_tuneSatellite.clear();
    }
    removeCreatedChannel();

    m_pendingTune = {true, satellite, frequency, mode, deviceSetIndex, type};
    if (!ChannelWebAPIUtils::addChannel(deviceSetIndex, uri, 0)) {
        m_pendingTune.m_active = false;
    }
}

void SatelliteTrackerGUI::channelAdded(int deviceSetIndex, ChannelAPI *channel)
{
    if (m_pendingTune.m_active
        && (deviceSetIndex == m_pendingTune.m_deviceSetIndex)
        && (channel->getIdentifier() == m_pendingTune.m_type))
    {
        PendingTune pending = m_pendingTune;
        m_pendingTune.m_active = false;
        m_createdChannel = channel;
        m_createdChannelDeviceSetIndex = deviceSetIndex;
        // Wait for channel GUI to be created and apply its initial settings, before we configure it
        QTimer::singleShot(0, this, [this, pending, channel]() {
            startTune(pending.m_satellite, pending.m_frequency, pending.m_mode, pending.m_deviceSetIndex, channel, pending.m_type, true, 0);
        });
    }
}

// Channel indices may have changed, or the channel we're tracking may have been removed
void SatelliteTrackerGUI::channelRemoved(int deviceSetIndex, ChannelAPI *channel)
{
    (void) deviceSetIndex;
    (void) channel;

    QTimer::singleShot(0, this, [this]() {
        if (m_createdChannel && (channelIndex(m_createdChannelDeviceSetIndex, m_createdChannel) < 0)) {
            m_createdChannel = nullptr;
        }
        if (!m_tuneSatellite.isEmpty())
        {
            int index = channelIndex(m_tuneDeviceSetIndex, m_tuneChannel);
            if (index < 0) {
                stopTune();
            } else if (index != m_tuneChannelIndex) {
                startTune(m_tuneSatellite, m_tuneFrequency, m_tuneMode, m_tuneDeviceSetIndex, m_tuneChannel, m_tuneChannelType, false, m_tuneOffset);
            }
        }
    });
}

// offset is how far the user has retuned from the transmitter's frequency
void SatelliteTrackerGUI::startTune(const QString& satellite, qint64 frequency, TuneMode mode, int deviceSetIndex, ChannelAPI *channel, const QString& type, bool configure, qint64 offset)
{
    int index = channelIndex(deviceSetIndex, channel);
    if (index < 0) {
        return;
    }

    RxChannel rxChannel = {deviceSetIndex, index, type, "", channel};
    bool sidebandMode = (mode == TUNE_USB) || (mode == TUNE_LSB) || (mode == TUNE_CW);
    bool sidebandChannel = (type == "SSBDemod") || (type == "WDSPRx");
    qint64 tuneFrequency = frequency;

    if (configure) {
        configureChannel(rxChannel, mode);
    }

    // Tune CW carrier to be heard as a tone in USB
    if ((mode == TUNE_CW) && sidebandChannel) {
        tuneFrequency -= m_cwToneFrequency;
    }

    m_tuneId++;
    m_tuneBaseFrequency = tuneFrequency;
    m_tuneOffset = offset;
    m_satelliteTracker->getInputMessageQueue()->push(SatelliteTracker::MsgTuneChannel::create(
        satellite, deviceSetIndex, index, tuneFrequency + offset, sidebandMode || sidebandChannel, m_tuneId));
    m_tuneSatellite = satellite;
    m_tuneFrequency = frequency;
    m_tuneMode = mode;
    m_tuneDeviceSetIndex = deviceSetIndex;
    m_tuneChannelIndex = index;
    m_tuneChannel = channel;
    m_tuneChannelType = type;
    if (channel == m_createdChannel) {
        m_createdChannelDeviceSetIndex = deviceSetIndex;
    }
}

void SatelliteTrackerGUI::stopTune()
{
    m_pendingTune.m_active = false;
    if (!m_tuneSatellite.isEmpty() && (m_satelliteTracker->getState() == Feature::StRunning)) {
        m_satelliteTracker->getInputMessageQueue()->push(SatelliteTracker::MsgTuneChannel::createStop());
    }
    m_tuneSatellite.clear();
    m_tuneChannel = nullptr;
    removeCreatedChannel();
}

// Remove demodulator that we created
void SatelliteTrackerGUI::removeCreatedChannel()
{
    if (m_createdChannel)
    {
        int index = channelIndex(m_createdChannelDeviceSetIndex, m_createdChannel);
        if (index >= 0) {
            MainCore::instance()->getMainMessageQueue()->push(MainCore::MsgDeleteChannel::create(m_createdChannelDeviceSetIndex, index));
        }
        m_createdChannel = nullptr;
    }
}

void SatelliteTrackerGUI::on_satTableHeader_sortIndicatorChanged(int logicalIndex, Qt::SortOrder order)
{
    m_settings.m_columnSort = logicalIndex;
    m_settings.m_columnSortOrder = order;
    m_settingsKeys.append("columnSort");
    m_settingsKeys.append("columnSortOrder");

    applySettings();
}

// Columns in table reordered
void SatelliteTrackerGUI::satTable_sectionMoved(int logicalIndex, int oldVisualIndex, int newVisualIndex)
{
    (void) oldVisualIndex;
    m_settings.m_columnIndexes[logicalIndex] = newVisualIndex;
}

// Column in table resized (when hidden size is 0)
void SatelliteTrackerGUI::satTable_sectionResized(int logicalIndex, int oldSize, int newSize)
{
    (void) oldSize;
    m_settings.m_columnSizes[logicalIndex] = newSize;
}

// Right click in table header - show column select menu
void SatelliteTrackerGUI::columnSelectMenu(QPoint pos)
{
    menu->popup(ui->satTable->horizontalHeader()->viewport()->mapToGlobal(pos));
}

// Hide/show column when menu selected
void SatelliteTrackerGUI::columnSelectMenuChecked(bool checked)
{
    (void) checked;
    QAction* action = qobject_cast<QAction*>(sender());

    if (action != nullptr)
    {
        int idx = action->data().toInt(nullptr);
        ui->satTable->setColumnHidden(idx, !action->isChecked());
    }
}

// Create column select menu item
QAction *SatelliteTrackerGUI::createCheckableItem(QString &text, int idx, bool checked)
{
    QAction *action = new QAction(text, this);
    action->setCheckable(true);
    action->setChecked(checked);
    action->setData(QVariant(idx));
    connect(action, SIGNAL(triggered()), this, SLOT(columnSelectMenuChecked()));
    return action;
}

void SatelliteTrackerGUI::updateDeviceFeatureCombo()
{
    if (m_settings.m_dateTimeSelect == SatelliteTrackerSettings::FROM_MAP) {
        updateMapList();
    } else if (m_settings.m_dateTimeSelect == SatelliteTrackerSettings::FROM_FILE) {
        updateFileInputList();
    }
}

void SatelliteTrackerGUI::updateDeviceFeatureCombo(const QStringList &items, const QString &selected)
{
    // Remove items no longer in list
    int i = 0;
    while (i < ui->deviceFeatureSelect->count())
    {
        if (!items.contains(ui->deviceFeatureSelect->itemText(i))) {
            ui->deviceFeatureSelect->removeItem(i);
        } else {
            i++;
        }
    }
    // Add new items to list
    for (auto item : items)
    {
        int idx = ui->deviceFeatureSelect->findText(item);

        if (idx == -1) {
            ui->deviceFeatureSelect->addItem(item);
        }
    }

    ui->deviceFeatureSelect->setCurrentIndex(ui->deviceFeatureSelect->findText(selected));
}

void SatelliteTrackerGUI::updateFileInputList()
{
    // Create list of File Input devices
    std::vector<DeviceSet*>& deviceSets = MainCore::instance()->getDeviceSets();
    int deviceIndex = 0;
    QStringList items;

    for (std::vector<DeviceSet*>::const_iterator it = deviceSets.begin(); it != deviceSets.end(); ++it, deviceIndex++)
    {
        if ((*it)->m_deviceAPI && (*it)->m_deviceAPI->getHardwareId() == "FileInput") {
           items.append(QString("R%1").arg(deviceIndex));
        }
    }

    updateDeviceFeatureCombo(items, m_settings.m_fileInputDevice);
}

void SatelliteTrackerGUI::updateMapList()
{
    // Create list of Map features
    std::vector<FeatureSet*>& featureSets = MainCore::instance()->getFeatureeSets();
    int featureIndex = 0;
    QStringList items;

    for (std::vector<FeatureSet*>::const_iterator it = featureSets.begin(); it != featureSets.end(); ++it, featureIndex++)
    {
        for (int fi = 0; fi < (*it)->getNumberOfFeatures(); fi++)
        {
            Feature *feature = (*it)->getFeatureAt(fi);

            if (feature->getURI() == "sdrangel.feature.map") {
                items.append(QString("F%1:%2").arg(featureIndex).arg(fi));
            }
        }
    }

    updateDeviceFeatureCombo(items, m_settings.m_mapFeature);
}

void SatelliteTrackerGUI::on_deviceFeatureSelect_currentIndexChanged(int index)
{
    (void) index;

    if (m_settings.m_dateTimeSelect == SatelliteTrackerSettings::FROM_MAP)
    {
        m_settings.m_mapFeature = ui->deviceFeatureSelect->currentText();
        m_settingsKeys.append("mapFeature");
    } else
    {
        m_settings.m_fileInputDevice = ui->deviceFeatureSelect->currentText();
        m_settingsKeys.append("fileInputDevice");
    }

    applySettings();
}

// Fill satellite group combo box, which is only shown when there are groups
void SatelliteTrackerGUI::updateSatelliteGroups()
{
    QSignalBlocker blocker(ui->satelliteGroup);
    ui->satelliteGroup->clear();
    ui->satelliteGroup->addItem("(none)");
    ui->satelliteGroup->addItems(m_settings.m_satelliteGroups.keys());
    int index = ui->satelliteGroup->findText(m_settings.m_satelliteGroup);
    ui->satelliteGroup->setCurrentIndex(m_settings.m_satelliteGroup.isEmpty() || (index < 0) ? 0 : index);
    ui->satelliteGroup->setVisible(!m_settings.m_satelliteGroups.isEmpty());
}

// Track the satellites in the selected group. "(none)" keeps the current satellites
void SatelliteTrackerGUI::on_satelliteGroup_currentIndexChanged(int index)
{
    if (index > 0)
    {
        QString group = ui->satelliteGroup->itemText(index);
        m_settings.m_satelliteGroup = group;
        m_settings.m_satellites = m_settings.m_satelliteGroups.value(group);
        m_settingsKeys.append("satellites");
        updateSelectedSats();
    }
    else
    {
        m_settings.m_satelliteGroup = "";
    }
    m_settingsKeys.append("satelliteGroup");
    applySettings();
}

void SatelliteTrackerGUI::makeUIConnections()
{
    QObject::connect(ui->satelliteGroup, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SatelliteTrackerGUI::on_satelliteGroup_currentIndexChanged);
    QObject::connect(ui->startStop, &ButtonSwitch::toggled, this, &SatelliteTrackerGUI::on_startStop_toggled);
    QObject::connect(ui->useMyPosition, &QToolButton::clicked, this, &SatelliteTrackerGUI::on_useMyPosition_clicked);
    QObject::connect(ui->latitude, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &SatelliteTrackerGUI::on_latitude_valueChanged);
    QObject::connect(ui->longitude, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &SatelliteTrackerGUI::on_longitude_valueChanged);
    QObject::connect(ui->target, &QComboBox::currentTextChanged, this, &SatelliteTrackerGUI::on_target_currentTextChanged);
    QObject::connect(ui->displaySettings, &QToolButton::clicked, this, &SatelliteTrackerGUI::on_displaySettings_clicked);
    QObject::connect(ui->radioControl, &QToolButton::clicked, this, &SatelliteTrackerGUI::on_radioControl_clicked);
    QObject::connect(ui->dateTimeSelect, qOverload<int>(&QComboBox::currentIndexChanged), this, &SatelliteTrackerGUI::on_dateTimeSelect_currentIndexChanged);
    QObject::connect(ui->dateTime, &WrappingDateTimeEdit::dateTimeChanged, this, &SatelliteTrackerGUI::on_dateTime_dateTimeChanged);
    QObject::connect(ui->viewOnMap, &QToolButton::clicked, this, &SatelliteTrackerGUI::on_viewOnMap_clicked);
    QObject::connect(ui->updateSatData, &QToolButton::clicked, this, &SatelliteTrackerGUI::on_updateSatData_clicked);
    QObject::connect(ui->selectSats, &QToolButton::clicked, this, &SatelliteTrackerGUI::on_selectSats_clicked);
    QObject::connect(ui->autoTarget, &ButtonSwitch::clicked, this, &SatelliteTrackerGUI::on_autoTarget_clicked);
    QObject::connect(ui->chartSelect, qOverload<int>(&QComboBox::currentIndexChanged), this, &SatelliteTrackerGUI::on_chartSelect_currentIndexChanged);
    QObject::connect(ui->nextPass, &QToolButton::clicked, this, &SatelliteTrackerGUI::on_nextPass_clicked);
    QObject::connect(ui->prevPass, &QToolButton::clicked, this, &SatelliteTrackerGUI::on_prevPass_clicked);
    QObject::connect(ui->darkTheme, &QToolButton::clicked, this, &SatelliteTrackerGUI::on_darkTheme_clicked);
    QObject::connect(ui->satTable, &QTableWidget::cellDoubleClicked, this, &SatelliteTrackerGUI::on_satTable_cellDoubleClicked);
    QObject::connect(ui->satTable->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, &SatelliteTrackerGUI::on_satTableHeader_sortIndicatorChanged);
    QObject::connect(ui->deviceFeatureSelect, qOverload<int>(&QComboBox::currentIndexChanged), this, &SatelliteTrackerGUI::on_deviceFeatureSelect_currentIndexChanged);
}

