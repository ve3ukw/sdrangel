///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 maintech GmbH, Otto-Hahn-Str. 15, 97204 Hoechberg, Germany //
// written by Christian Daniel                                                   //
// Copyright (C) 2015-2022 Edouard Griffiths, F4EXB <f4exb06@gmail.com>          //
// Copyright (C) 2015 John Greb <hexameron@spam.no>                              //
// Copyright (C) 2020-2023 Jon Beniston, M7RCE <jon@beniston.com>                //
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

#ifndef INCLUDE_FEATURE_SATELLITETRACKERGUI_H_
#define INCLUDE_FEATURE_SATELLITETRACKERGUI_H_

#include <QTimer>
#include <QMenu>
#include <QtCharts>
#include <QTextToSpeech>

#include "feature/featuregui.h"
#include "util/messagequeue.h"
#include "settings/rollupstate.h"

#include "satellitetrackersettings.h"
#include "satnogs.h"

class PluginAPI;
class FeatureUISet;
class SatelliteTracker;
class ChannelAPI;
struct SatelliteState;

namespace Ui {
    class SatelliteTrackerGUI;
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using namespace QtCharts;
#endif

class SatelliteTrackerGUI : public FeatureGUI {
    Q_OBJECT
public:
    static SatelliteTrackerGUI* create(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature);
    virtual void destroy();

    void resetToDefaults();
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
    virtual MessageQueue *getInputMessageQueue() { return &m_inputMessageQueue; }
    virtual void setWorkspaceIndex(int index);
    virtual int getWorkspaceIndex() const { return m_settings.m_workspaceIndex; }
    virtual void setGeometryBytes(const QByteArray& blob) { m_settings.m_geometryBytes = blob; }
    virtual QByteArray getGeometryBytes() const { return m_settings.m_geometryBytes; }

private:
    Ui::SatelliteTrackerGUI* ui;
    PluginAPI* m_pluginAPI;
    FeatureUISet* m_featureUISet;
    SatelliteTrackerSettings m_settings;
    QList<QString> m_settingsKeys;
    RollupState m_rollupState;
    bool m_doApplySettings;

    SatelliteTracker* m_satelliteTracker;
    MessageQueue m_inputMessageQueue;
    QTimer m_statusTimer;
    bool m_lastUpdatingSatData;

    QHash<QString, SatNogsSatellite *> m_satellites;
    SatelliteState *m_targetSatState;

    int m_plotPass;

    QChart m_emptyChart;
    QChart *m_lineChart;
    QPolarChart *m_polarChart;
    QTimer m_redrawTimer;

    QDateTime m_nextTargetAOS;
    QDateTime m_nextTargetLOS;
    bool m_geostationarySatVisible;

#ifdef QT_TEXTTOSPEECH_FOUND
    QTextToSpeech *m_speech;
#endif
    QMenu *menu;                        // Column select context menu

    QString m_tuneSatellite;            // Satellite a channel is tuned to from the table context menu, empty if none
    qint64 m_tuneFrequency;
    int m_tuneDeviceSetIndex;
    int m_tuneChannelIndex;

    enum TuneMode {
        TUNE_OTHER,
        TUNE_FM,
        TUNE_SSTV,              // SSTV images on FM
        TUNE_AFSK,              // 1200 baud AFSK (E.g. APRS)
        TUNE_G3RUH,             // 9600 baud G3RUH AX.25
        TUNE_USB,
        TUNE_LSB,
        TUNE_CW,
        TUNE_LORA,
        TUNE_APT,
        TUNE_DVBS
    };
    TuneMode m_tuneMode;
    ChannelAPI *m_tuneChannel;          // Channel being tracked. Index can change when other channels are removed
    QString m_tuneChannelType;
    ChannelAPI *m_createdChannel;       // Demodulator we created, which we remove when no longer used
    int m_createdChannelDeviceSetIndex;
    int m_tuneId;                       // Matches MsgReportTune to latest MsgTuneChannel
    qint64 m_tuneBaseFrequency;         // Frequency sent to worker, before user retuning (includes CW tone offset)
    qint64 m_tuneOffset;                // How far user has retuned the channel from the transmitter frequency
    float m_tuneCTCSS;                  // Uplink CTCSS tone (Hz) for transmitter, or 0. For when we support Tx
    struct PendingTune {                // Tune to perform once a new demodulator has been created
        bool m_active;
        QString m_satellite;
        qint64 m_frequency;
        TuneMode m_mode;
        int m_deviceSetIndex;
        QString m_type;
    } m_pendingTune;
    static const int m_cwToneFrequency = 700;  // CW tone in Hz, when tuning CW with an SSB demodulator

    struct RxChannel {
        int m_deviceSetIndex;
        int m_channelIndex;
        QString m_type;             // E.g. NFMDemod
        QString m_label;
        ChannelAPI *m_channel;
    };

    enum SatCol {
        SAT_COL_NAME,
        SAT_COL_AZ,
        SAT_COL_EL,
        SAT_COL_TNE,
        SAT_COL_DUR,
        SAT_COL_AOS,
        SAT_COL_LOS,
        SAT_COL_MAX_EL,
        SAT_COL_DIR,
        SAT_COL_LATITUDE,
        SAT_COL_LONGITUDE,
        SAT_COL_ALT,
        SAT_COL_RANGE,
        SAT_COL_RANGE_RATE,
        SAT_COL_DOPPLER,
        SAT_COL_PATH_LOSS,
        SAT_COL_DELAY,
        SAT_COL_NORAD_ID
    };

    explicit SatelliteTrackerGUI(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature, QWidget* parent = nullptr);
    virtual ~SatelliteTrackerGUI();

    void aos(const QString &speech);
    void los(const QString &speech);

    void blockApplySettings(bool block);
    void applySettings(bool force = false);
    void displaySettings();
    void setTarget(const QString& target);
    QString convertDegreesToText(double degrees);
    bool handleMessage(const Message& message);
    void plotChart();
    void plotAzElChart();
    void plotPolarChart();
    void resizeTable();
    void updateTable(SatelliteState *satState);
    void updateSelectedSats();
    QAction *createCheckableItem(QString& text, int idx, bool checked);
    void updateTimeToAOS();
    QString formatDaysTime(qint64 days, QDateTime dateTime);
    QString formatSecondsAsHHMMSS(qint64 seconds);
    void updateDeviceFeatureCombo();
    void updateDeviceFeatureCombo(const QStringList &items, const QString &selected);
    void updateFileInputList();
    void updateMapList();
    void makeUIConnections();
    void limitAzElRange(double& azimuth, double& elevation) const;
    void tuneChannel(const QString& satellite, qint64 frequency, TuneMode mode, const RxChannel& rxChannel, qint64 offset = 0);
    void configureChannel(const RxChannel& rxChannel, TuneMode mode);
    void tuneNewChannel(const QString& satellite, qint64 frequency, TuneMode mode, int deviceSetIndex, const QString& type);
    void startTune(const QString& satellite, qint64 frequency, TuneMode mode, int deviceSetIndex, ChannelAPI *channel, const QString& type, bool configure, qint64 offset);
    void removeCreatedChannel();
    void workSatellite(const QString& satellite);
    void startRotators();
    static QString availableChannelType(const QStringList& types);
    static int channelIndex(int deviceSetIndex, const ChannelAPI *channel);
    static TuneMode tuneMode(const SatNogsTransmitter *transmitter);
    static QStringList channelTypesForMode(TuneMode mode);
    static int channelForMode(TuneMode mode, const QList<RxChannel>& rxChannels, int selectedChannel);

private slots:
    void onMenuDialogCalled(const QPoint &p);
    void onWidgetRolled(QWidget* widget, bool rollDown);
    void handleInputMessages();
    void on_startStop_toggled(bool checked);
    void on_useMyPosition_clicked(bool checked=false);
    void on_latitude_valueChanged(double value);
    void on_longitude_valueChanged(double value);
    void on_target_currentTextChanged(const QString &text);
    void on_displaySettings_clicked();
    void on_radioControl_clicked();
    void on_dateTimeSelect_currentIndexChanged(int index);
    void on_dateTime_dateTimeChanged(const QDateTime &datetime);
    void updateStatus();
    void updateFeatureState();
    void on_viewOnMap_clicked();
    void on_updateSatData_clicked();
    void on_selectSats_clicked();
    void on_autoTarget_clicked(bool checked);
    void on_chartSelect_currentIndexChanged(int index);
    void on_nextPass_clicked();
    void on_prevPass_clicked();
    void on_darkTheme_clicked(bool checked);
    void on_satTable_cellDoubleClicked(int row, int column);
    void satTable_sectionMoved(int logicalIndex, int oldVisualIndex, int newVisualIndex);
    void satTable_sectionResized(int logicalIndex, int oldSize, int newSize);
    void on_satTableHeader_sortIndicatorChanged(int logicalIndex, Qt::SortOrder order);
    void columnSelectMenu(QPoint pos);
    void satTable_customContextMenuRequested(QPoint pos);
    void stopTune();
    void channelAdded(int deviceSetIndex, ChannelAPI *channel);
    void channelRemoved(int deviceSetIndex, ChannelAPI *channel);
    void columnSelectMenuChecked(bool checked = false);
    void on_deviceFeatureSelect_currentIndexChanged(int index);
};


#endif // INCLUDE_FEATURE_SATELLITETRACKERGUI_H_
