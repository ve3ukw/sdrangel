///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2021-2023 Jon Beniston, M7RCE <jon@beniston.com>                //
// Copyright (C) 2021-2022 Edouard Griffiths, F4EXB <f4exb06@gmail.com>          //
// Copyright (C) 2022 Jiří Pinkava <jiri.pinkava@rossum.ai>                      //
// Copyright (C) 2023 Daniele Forsi <iu5hkx@gmail.com>                           //
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

#include <QDebug>
#include <QAbstractSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QEventLoop>
#include <QTimer>
#include <QDateTime>

#include "SWGTargetAzimuthElevation.h"
#include "SWGMapItem.h"

#include "webapi/webapiadapterinterface.h"

#include "util/units.h"
#include "device/deviceset.h"
#include "device/deviceapi.h"
#include "dsp/devicesamplesource.h"
#include "dsp/devicesamplemimo.h"
#include "channel/channelapi.h"
#include "channel/channelwebapiutils.h"
#include "feature/featurewebapiutils.h"
#include "maincore.h"

#include "satellitetracker.h"
#include "satellitetrackerworker.h"
#include "satellitetrackerreport.h"
#include "satellitetrackersgp4.h"

MESSAGE_CLASS_DEFINITION(SatelliteTrackerWorker::MsgConfigureSatelliteTrackerWorker, Message)
MESSAGE_CLASS_DEFINITION(SatelliteTrackerReport::MsgReportSat, Message)
MESSAGE_CLASS_DEFINITION(SatelliteTrackerReport::MsgReportAOS, Message)
MESSAGE_CLASS_DEFINITION(SatelliteTrackerReport::MsgReportLOS, Message)
MESSAGE_CLASS_DEFINITION(SatelliteTrackerReport::MsgReportTarget, Message)
MESSAGE_CLASS_DEFINITION(SatelliteTrackerReport::MsgReportTune, Message)

SatelliteTrackerWorker::SatelliteTrackerWorker(SatelliteTracker* satelliteTracker, WebAPIAdapterInterface *webAPIAdapterInterface) :
    m_satelliteTracker(satelliteTracker),
    m_webAPIAdapterInterface(webAPIAdapterInterface),
    m_msgQueueToFeature(nullptr),
    m_msgQueueToGUI(nullptr),
    m_pollTimer(this),
    m_recalculatePasses(true),
    m_flipRotation(false),
    m_extendedAzRotation(false),
    m_tuneTimer(this),
    m_tuneDeviceSetIndex(-1),
    m_tuneChannelIndex(-1),
    m_tuneFrequency(0),
    m_tuneFastDoppler(false),
    m_tuneId(0),
    m_tuneLastFrequency(-1),
    m_tuneDeviceRetuned(false)
{
    connect(&m_pollTimer, SIGNAL(timeout()), this, SLOT(update()));
    connect(&m_tuneTimer, &QTimer::timeout, this, &SatelliteTrackerWorker::tuneDoppler);
}

SatelliteTrackerWorker::~SatelliteTrackerWorker()
{
    qDebug() << "SatelliteTrackerWorker::~SatelliteTrackerWorker";
    stopWork();
    m_inputMessageQueue.clear();
    // Remove satellites from Map
    QHashIterator<QString, SatWorkerState *> itr(m_workerState);
    while (itr.hasNext())
    {
        itr.next();
        if (m_settings.m_drawOnMap) {
            removeFromMap(itr.key());
        }
    }
    qDeleteAll(m_workerState);
}

void SatelliteTrackerWorker::startWork()
{
    qDebug() << "SatelliteTrackerWorker::startWork";
    QMutexLocker mutexLocker(&m_mutex);
    connect(&m_inputMessageQueue, SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));
    m_recalculatePasses = true;

     m_pollTimer.start((int)round(m_settings.m_updatePeriod*1000.0));
    // Resume doppler timers
    QHashIterator<QString, SatWorkerState *> itr(m_workerState);
    while (itr.hasNext())
    {
        itr.next();
        SatWorkerState *satWorkerState = itr.value();
        if (satWorkerState->m_dopplerTimer.interval() > 0)
            satWorkerState->m_dopplerTimer.start();
    }
    if (!m_tuneSatellite.isEmpty()) {
        m_tuneTimer.start();
    }

    // Handle any messages already on the queue
    handleInputMessages();
}

void SatelliteTrackerWorker::stopWork()
{
    qDebug() << "SatelliteTrackerWorker::stopWork";
    QMutexLocker mutexLocker(&m_mutex);
    disconnect(&m_inputMessageQueue, SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));
    m_pollTimer.stop();
    // Stop doppler timers
    QHashIterator<QString, SatWorkerState *> itr(m_workerState);
    while (itr.hasNext())
    {
        itr.next();
        itr.value()->m_dopplerTimer.stop();
    }
    m_tuneTimer.stop();
}

void SatelliteTrackerWorker::handleInputMessages()
{
    Message* message;

    while ((message = m_inputMessageQueue.pop()))
    {
        if (!handleMessage(*message)) {
            qDebug("%s: unhandled message: %s", Q_FUNC_INFO, message->getIdentifier());
        }
        delete message;
    }
}

bool SatelliteTrackerWorker::handleMessage(const Message& message)
{
    if (MsgConfigureSatelliteTrackerWorker::match(message))
    {
        QMutexLocker mutexLocker(&m_mutex);
        MsgConfigureSatelliteTrackerWorker& cfg = (MsgConfigureSatelliteTrackerWorker&) message;

        applySettings(cfg.getSettings(), cfg.getSettingsKeys(), cfg.getForce());
        return true;
    }
    else if (SatelliteTracker::MsgSatData::match(message))
    {
        SatelliteTracker::MsgSatData& satData = (SatelliteTracker::MsgSatData&) message;
        m_satellites = satData.getSatellites();
        m_recalculatePasses = true;
        return true;
    }
    else if (SatelliteTracker::MsgTuneChannel::match(message))
    {
        QMutexLocker mutexLocker(&m_mutex);
        SatelliteTracker::MsgTuneChannel& tune = (SatelliteTracker::MsgTuneChannel&) message;

        if (tune.getFrequency() > 0) {
            startTune(tune.getSatellite(), tune.getDeviceSetIndex(), tune.getChannelIndex(), tune.getFrequency(), tune.getFastDoppler(), tune.getTuneId(), tune.getCompanionChannelIndexes());
        } else {
            stopTune();
        }
        return true;
    }
    else
    {
        return false;
    }
}

void SatelliteTrackerWorker::applySettings(const SatelliteTrackerSettings& settings, const QList<QString>& settingsKeys, bool force)
{
    qDebug() << "SatelliteTrackerWorker::applySettings:" << settings.getDebugString(settingsKeys, force) << " force: " << force;

    if (settingsKeys.contains("target")
        || settingsKeys.contains("latitude")
        || settingsKeys.contains("longitude")
        || settingsKeys.contains("heightAboveSeaLevel")
        || settingsKeys.contains("dateTime")
        || settingsKeys.contains("utc")
        || settingsKeys.contains("groundTrackPoints")
        || settingsKeys.contains("minAOSElevation")
        || settingsKeys.contains("minPassElevation")
        || settingsKeys.contains("predictionPeriod")
        || settingsKeys.contains("passStartTime")
        || settingsKeys.contains("passFinishTime")
        || (!m_settings.m_drawOnMap && settings.m_drawOnMap)
        || force)
    {
        // Recalculate immediately
        m_recalculatePasses = true;
        QTimer::singleShot(1, this, &SatelliteTrackerWorker::update);
        m_pollTimer.start((int)round(settings.m_updatePeriod*1000.0));
    }
    else if (settingsKeys.contains("updatePeriod") || force)
    {
        m_pollTimer.start((int)round(settings.m_updatePeriod*1000.0));
    }

    if (!settings.m_drawOnMap && m_settings.m_drawOnMap)
    {
        QHashIterator<QString, SatWorkerState *> itr(m_workerState);
        while (itr.hasNext())
        {
            itr.next();
            removeFromMap(itr.key());
        }
    }

    // Remove satellites no longer needed
    QMutableHashIterator<QString, SatWorkerState *> itr(m_workerState);
    while (itr.hasNext())
    {
        itr.next();
        if (settings.m_satellites.indexOf(itr.key()) == -1)
        {
            if (m_settings.m_drawOnMap) {
                removeFromMap(itr.key());
            }
            delete itr.value();
            itr.remove();
        }
    }

    // Add new satellites
    for (int i = 0; i < settings.m_satellites.size(); i++)
    {
        if (!m_workerState.contains(settings.m_satellites[i]))
        {
            SatWorkerState *satWorkerState = new SatWorkerState(settings.m_satellites[i]);
            m_workerState.insert(settings.m_satellites[i], satWorkerState);
            connect(&satWorkerState->m_aosTimer, &QTimer::timeout, [this, satWorkerState]() {
                aos(satWorkerState);
            });
            connect(&satWorkerState->m_losTimer, &QTimer::timeout, [this, satWorkerState]() {
                los(satWorkerState);
            });
            m_recalculatePasses = true;
        }
    }

    if (settingsKeys.contains("target") && (settings.m_target != m_settings.m_target))
    {
        m_prePositionedAOS = QDateTime();   // So new target is pre-positioned
        if (m_workerState.contains(m_settings.m_target))
        {
            SatWorkerState *satWorkerState = m_workerState.value(m_settings.m_target);
            disableDoppler(satWorkerState);
        }
        if (m_workerState.contains(settings.m_target))
        {
            SatWorkerState *satWorkerState = m_workerState.value(settings.m_target);
            if (satWorkerState->hasAOS(m_satelliteTracker->currentDateTimeUtc())) {
                enableDoppler(satWorkerState);
            }
        }
    }

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    if ((settingsKeys.contains("dopplerPeriod") || force) && !m_tuneSatellite.isEmpty()) {
        m_tuneTimer.setInterval(tuneInterval());
    }
}

void SatelliteTrackerWorker::removeFromMap(QString id)
{
    QList<ObjectPipe*> mapMessagePipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(m_satelliteTracker, "mapitems", mapMessagePipes);

    if (mapMessagePipes.size() > 0) {
        sendToMap(mapMessagePipes, id, "", "", "", 0.0f, 0.0, 0.0, 0.0, 0.0, nullptr, nullptr, nullptr, nullptr);
    }
}

void SatelliteTrackerWorker::sendToMap(
    const QList<ObjectPipe*>& mapMessagePipes,
    QString name,
    QString image,
    QString model,
    QString text,
    float labelOffset,
    double lat,
    double lon,
    double altitude,
    double rotation,
    QList<QGeoCoordinate *> *track,
    QList<QDateTime *> *trackDateTime,
    QList<QGeoCoordinate *> *predictedTrack,
    QList<QDateTime *> *predictedTrackDateTime
)
{
    for (const auto& pipe : mapMessagePipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);
        SWGSDRangel::SWGMapItem *swgMapItem = new SWGSDRangel::SWGMapItem();
        swgMapItem->setName(new QString(name));
        swgMapItem->setLatitude(lat);
        swgMapItem->setLongitude(lon);
        swgMapItem->setAltitude(altitude);
        swgMapItem->setImage(new QString(image));
        swgMapItem->setImageRotation(rotation);
        swgMapItem->setText(new QString(text));
        swgMapItem->setModel(new QString(model));
        swgMapItem->setFixedPosition(false);
        swgMapItem->setOrientation(0);
        swgMapItem->setLabel(new QString(name));
        swgMapItem->setLabelAltitudeOffset(labelOffset);
        if (track != nullptr)
        {
            QList<SWGSDRangel::SWGMapCoordinate *> *mapTrack = new QList<SWGSDRangel::SWGMapCoordinate *>();
            for (int i = 0; i < track->size(); i++)
            {
                SWGSDRangel::SWGMapCoordinate* p = new SWGSDRangel::SWGMapCoordinate();
                QGeoCoordinate *c = track->at(i);
                p->setLatitude(c->latitude());
                p->setLongitude(c->longitude());
                p->setAltitude(c->altitude());
                p->setDateTime(new QString(trackDateTime->at(i)->toString(Qt::ISODate)));
                mapTrack->append(p);
            }
            swgMapItem->setTrack(mapTrack);
        }
        if (predictedTrack != nullptr)
        {
            QList<SWGSDRangel::SWGMapCoordinate *> *mapTrack = new QList<SWGSDRangel::SWGMapCoordinate *>();
            for (int i = 0; i < predictedTrack->size(); i++)
            {
                SWGSDRangel::SWGMapCoordinate* p = new SWGSDRangel::SWGMapCoordinate();
                QGeoCoordinate *c = predictedTrack->at(i);
                p->setLatitude(c->latitude());
                p->setLongitude(c->longitude());
                p->setAltitude(c->altitude());
                p->setDateTime(new QString(predictedTrackDateTime->at(i)->toString(Qt::ISODate)));
                mapTrack->append(p);
            }
            swgMapItem->setPredictedTrack(mapTrack);
        }

        MainCore::MsgMapItem *msg = MainCore::MsgMapItem::create(m_satelliteTracker, swgMapItem);
        messageQueue->push(msg);
    }
}

void SatelliteTrackerWorker::update()
{
    // Get date and time to calculate position at
    QDateTime qdt;
    if (m_settings.m_dateTime == "")
        qdt = m_satelliteTracker->currentDateTimeUtc();
    else if (m_settings.m_utc)
        qdt = QDateTime::fromString(m_settings.m_dateTime, Qt::ISODateWithMs);
    else
        qdt = QDateTime::fromString(m_settings.m_dateTime, Qt::ISODateWithMs).toUTC();

    bool timeReversed = m_lastUpdateDateTime > qdt;

    QHashIterator<QString, SatWorkerState *> itr(m_workerState);
    while (itr.hasNext())
    {
        itr.next();
        SatWorkerState *satWorkerState = itr.value();
        QString name = satWorkerState->m_name;
        if (m_satellites.contains(name))
        {
            SatNogsSatellite *sat = m_satellites.value(name);
            if (sat->m_tle != nullptr)
            {
                // Calculate position, AOS/LOS and other details for satellite
                int noOfPasses;
                bool recalcAsPastLOS = (satWorkerState->m_satState.m_passes.size() > 0) && (satWorkerState->m_satState.m_passes[0].m_los < qdt);
                if (m_recalculatePasses || recalcAsPastLOS || timeReversed)
                    noOfPasses = (name == m_settings.m_target) ? 99 : 1;
                else
                    noOfPasses = 0;
                getSatelliteState(qdt, *sat->m_tle,
                                    m_settings.m_latitude, m_settings.m_longitude, m_settings.m_heightAboveSeaLevel/1000.0,
                                    m_settings.m_predictionPeriod, m_settings.m_minAOSElevation, m_settings.m_minPassElevation,
                                    m_settings.m_passStartTime, m_settings.m_passFinishTime, m_settings.m_utc,
                                    noOfPasses, m_settings.m_groundTrackPoints, &satWorkerState->m_satState);

                // Update AOS/LOS
                if (satWorkerState->m_satState.m_passes.size() > 0)
                {
                    // Only use timers if using real time
                    if (m_settings.m_dateTimeSelect == SatelliteTrackerSettings::NOW)
                    {
                        // Do we have a new pass?
                        if ((satWorkerState->m_aos != satWorkerState->m_satState.m_passes[0].m_aos) || (satWorkerState->m_los != satWorkerState->m_satState.m_passes[0].m_los))
                        {
                            qDebug() << "SatelliteTrackerWorker: Current time: " << qdt.toString(Qt::ISODateWithMs);
                            qDebug() << "SatelliteTrackerWorker: New AOS: " << name << " new: " << satWorkerState->m_satState.m_passes[0].m_aos << " old: " << satWorkerState->m_aos;
                            qDebug() << "SatelliteTrackerWorker: New LOS: " << name << " new: " << satWorkerState->m_satState.m_passes[0].m_los << " old: " << satWorkerState->m_los;
                            satWorkerState->m_aos = satWorkerState->m_satState.m_passes[0].m_aos;
                            satWorkerState->m_los = satWorkerState->m_satState.m_passes[0].m_los;
                            satWorkerState->m_hasSignalledAOS = false;
                            if (satWorkerState->m_aos.isValid())
                            {
                                if (satWorkerState->m_aos > qdt)
                                {
                                    satWorkerState->m_aosTimer.setInterval(satWorkerState->m_aos.toMSecsSinceEpoch() - qdt.toMSecsSinceEpoch());
                                    satWorkerState->m_aosTimer.setSingleShot(true);
                                    satWorkerState->m_aosTimer.start();
                                }
                                else if (qdt < satWorkerState->m_los)
                                    aos(satWorkerState);

                                if (satWorkerState->m_los.isValid() && (m_settings.m_target == satWorkerState->m_name))
                                    calculateRotation(satWorkerState);
                            }
                            if (satWorkerState->m_los.isValid() && (satWorkerState->m_los > qdt))
                            {
                                if (satWorkerState->m_losTimer.isActive()) {
                                    qDebug() << "SatelliteTrackerWorker::update m_losTimer.remainingTime: " << satWorkerState->m_losTimer.remainingTime();
                                }
                                // We can detect a new AOS for a satellite, a little bit before the LOS has occurred
                                // Allow for 5s here (1s doesn't appear to be enough in some cases)
                                if (satWorkerState->m_losTimer.isActive() && (satWorkerState->m_losTimer.remainingTime() <= 5000))
                                {
                                    satWorkerState->m_losTimer.stop();
                                    // LOS hasn't been called yet - do so, before we reset timer
                                    los(satWorkerState);
                                }
                                qDebug() << "SatelliteTrackerWorker:: Interval to LOS " << (satWorkerState->m_los.toMSecsSinceEpoch() - qdt.toMSecsSinceEpoch());
                                satWorkerState->m_losTimer.setInterval(satWorkerState->m_los.toMSecsSinceEpoch() - qdt.toMSecsSinceEpoch());
                                satWorkerState->m_losTimer.setSingleShot(true);
                                satWorkerState->m_losTimer.start();
                            }
                        }
                    }
                    else
                    {
                        // Do we need to signal LOS?
                        if (satWorkerState->m_hasSignalledAOS && !satWorkerState->hasAOS(qdt))
                        {
                            los(satWorkerState);
                            satWorkerState->m_hasSignalledAOS = false;
                        }
                        // Do we have a new pass?
                        if ((satWorkerState->m_aos != satWorkerState->m_satState.m_passes[0].m_aos) || (satWorkerState->m_los != satWorkerState->m_satState.m_passes[0].m_los))
                        {
                            satWorkerState->m_aos = satWorkerState->m_satState.m_passes[0].m_aos;
                            satWorkerState->m_los = satWorkerState->m_satState.m_passes[0].m_los;
                            satWorkerState->m_hasSignalledAOS = false;
                        }
                        // Check if we need to signal AOS
                        if (!satWorkerState->m_hasSignalledAOS && satWorkerState->m_aos.isValid() && satWorkerState->hasAOS(qdt)) {
                            aos(satWorkerState);
                        }
                    }
                }
                else
                {
                    satWorkerState->m_aos = QDateTime();
                    satWorkerState->m_los = QDateTime();
                    satWorkerState->m_aosTimer.stop();
                    satWorkerState->m_losTimer.stop();
                }

                // Send Az/El of target to Rotator Controllers, if elevation above horizon
                if ((name == m_settings.m_target) && (satWorkerState->m_satState.m_elevation >= 0))
                {
                    sendToRotators(satWorkerState->m_satState.m_azimuth, satWorkerState->m_satState.m_elevation);
                }
                // Otherwise, point rotators at where the next pass will start, so they're ready at AOS
                else if ((name == m_settings.m_target)
                    && m_settings.m_rotatorPrePosition
                    && (m_settings.m_dateTimeSelect == SatelliteTrackerSettings::NOW)
                    && (satWorkerState->m_satState.m_passes.size() > 0)
                    && satWorkerState->m_satState.m_passes[0].m_aos.isValid()
                    && (satWorkerState->m_satState.m_passes[0].m_aos != m_prePositionedAOS))
                {
                    qDebug() << "SatelliteTrackerWorker::update: Pre-positioning rotators for AOS of" << name << "at" << satWorkerState->m_satState.m_passes[0].m_aos;
                    m_prePositionedAOS = satWorkerState->m_satState.m_passes[0].m_aos;
                    sendToRotators(satWorkerState->m_satState.m_passes[0].m_aosAzimuth, 0.0);
                }

                // Send to Map
                if (m_settings.m_drawOnMap)
                {
                    QList<ObjectPipe*> mapMessagePipes;
                    MainCore::instance()->getMessagePipes().getMessagePipes(m_satelliteTracker, "mapitems", mapMessagePipes);

                    if (mapMessagePipes.size() > 0)
                    {
                        const QStringList cubeSats({"AISAT-1", "FOX-1B", "FOX-1C", "FOX-1D", "FOX-1E", "FUNCUBE-1", "NO-84"});
                        QString image;
                        QString model;
                        float labelOffset;

                        if (sat->m_name == "ISS")
                        {
                            image = "qrc:///satellitetracker/satellitetracker/iss-32.png";
                            model = "iss.glb";
                            labelOffset = 15.0f;
                        }
                        else if (cubeSats.contains(sat->m_name))
                        {
                            image = "qrc:///satellitetracker/satellitetracker/cubesat-32.png";
                            model = "cubesat.glb";
                            labelOffset = 0.7f;
                        }
                        else
                        {
                            image = "qrc:///satellitetracker/satellitetracker/satellite-32.png";
                            model = "satellite.glb";
                            labelOffset = 2.5f;
                        }

                        QString text = QString("Name: %1\nAltitude: %2 km\nRange: %3 km\nRange rate: %4 km/s\nSpeed: %5 km/h\nPeriod: %6 mins")
                                               .arg(sat->m_name)
                                               .arg((int)round(satWorkerState->m_satState.m_altitude))
                                               .arg((int)round(satWorkerState->m_satState.m_range))
                                               .arg(satWorkerState->m_satState.m_rangeRate, 0, 'f', 1)
                                               .arg(Units::kmpsToIntegerKPH(satWorkerState->m_satState.m_speed))
                                               .arg((int)round(satWorkerState->m_satState.m_period));
                        if (satWorkerState->m_satState.m_passes.size() > 0)
                        {
                            if ((qdt >= satWorkerState->m_satState.m_passes[0].m_aos) && (qdt <= satWorkerState->m_satState.m_passes[0].m_los))
                                text = text.append("\nSatellite is visible");
                            else
                                text = text.append("\nAOS in: %1 mins").arg((int)round((satWorkerState->m_satState.m_passes[0].m_aos.toSecsSinceEpoch() - qdt.toSecsSinceEpoch())/60.0));
                            QString aosDateTime;
                            QString losDateTime;
                            if (m_settings.m_utc)
                            {
                                aosDateTime = satWorkerState->m_satState.m_passes[0].m_aos.toString(m_settings.m_dateFormat + " hh:mm");
                                losDateTime = satWorkerState->m_satState.m_passes[0].m_los.toString(m_settings.m_dateFormat + " hh:mm");
                            }
                            else
                            {
                                aosDateTime = satWorkerState->m_satState.m_passes[0].m_aos.toLocalTime().toString(m_settings.m_dateFormat + " hh:mm");
                                losDateTime = satWorkerState->m_satState.m_passes[0].m_los.toLocalTime().toString(m_settings.m_dateFormat + " hh:mm");
                            }
                            text = QString("%1\nAOS: %2\nLOS: %3\nMax El: %4%5")
                                            .arg(text)
                                            .arg(aosDateTime)
                                            .arg(losDateTime)
                                            .arg((int)round(satWorkerState->m_satState.m_passes[0].m_maxElevation))
                                            .arg(QChar(0xb0));
                        }

                        sendToMap(
                            mapMessagePipes,
                            sat->m_name,
                            image,
                            model,
                            text,
                            labelOffset,
                            satWorkerState->m_satState.m_latitude, satWorkerState->m_satState.m_longitude,
                            satWorkerState->m_satState.m_altitude * 1000.0, 0,
                            &satWorkerState->m_satState.m_groundTrack, &satWorkerState->m_satState.m_groundTrackDateTime,
                            &satWorkerState->m_satState.m_predictedGroundTrack, &satWorkerState->m_satState.m_predictedGroundTrackDateTime
                        );
                    }
                }

                // Send to GUI
                if (getMessageQueueToGUI())
                    getMessageQueueToGUI()->push(SatelliteTrackerReport::MsgReportSat::create(new SatelliteState(satWorkerState->m_satState)));

                // Sent to Feature for Web report
                if (m_msgQueueToFeature)
                    m_msgQueueToFeature->push(SatelliteTrackerReport::MsgReportSat::create(new SatelliteState(satWorkerState->m_satState)));
            }
            else
                qDebug() << "SatelliteTrackerWorker::update: No TLE for " << sat->m_name << ". Can't compute position.";
        }
    }
    m_lastUpdateDateTime = qdt;
    m_recalculatePasses = false;
}

void SatelliteTrackerWorker::aos(SatWorkerState *satWorkerState)
{
    qDebug() << "SatelliteTrackerWorker::aos " << satWorkerState->m_name;

    satWorkerState->m_hasSignalledAOS = true;

    // Indicate AOS to GUI
    if (getMessageQueueToGUI())
    {
        QString speech = substituteVariables(m_settings.m_aosSpeech, satWorkerState->m_name);
        getMessageQueueToGUI()->push(SatelliteTrackerReport::MsgReportAOS::create(satWorkerState->m_name, speech));
    }

    // Update target - but not while user is working the current target (Doppler tracking it during a pass)
    if (m_settings.m_autoTarget && (satWorkerState->m_name != m_settings.m_target))
    {
        // Only switch if higher priority (earlier in list) or other target not in AOS
        SatWorkerState *targetSatWorkerState = m_workerState.value(m_settings.m_target);
        bool targetHasAOS = targetSatWorkerState && targetSatWorkerState->hasAOS(m_satelliteTracker->currentDateTimeUtc());
        bool working = targetHasAOS && (m_tuneSatellite == m_settings.m_target);
        int currentTargetIdx = m_settings.m_satellites.indexOf(m_settings.m_target);
        int newTargetIdx = m_settings.m_satellites.indexOf(satWorkerState->m_name);
        if (!working && ((newTargetIdx < currentTargetIdx) || !targetHasAOS))
        {
            // Stop doppler correction for current target
            if (m_workerState.contains(m_settings.m_target))
                disableDoppler(m_workerState.value(m_settings.m_target));

            qDebug() << "SatelliteTrackerWorker::aos - autoTarget setting " << satWorkerState->m_name;
            m_settings.m_target = satWorkerState->m_name;
            // Update GUI with new target
            if (getMessageQueueToGUI())
                getMessageQueueToGUI()->push(SatelliteTrackerReport::MsgReportTarget::create(satWorkerState->m_name));
        }
    }

    // TODO: Detect if different device sets are used and support multiple sats simultaneously
    if (m_settings.m_target == satWorkerState->m_name)
        applyDeviceAOSSettings(satWorkerState->m_name);
}

// Send azimuth and elevation of target to Rotator Controllers, adjusting for offsets and rotator rotation range
void SatelliteTrackerWorker::sendToRotators(double azimuth, double elevation)
{
    azimuth += m_settings.m_azimuthOffset;
    elevation += m_settings.m_elevationOffset;
    if (m_extendedAzRotation)
    {
        if (azimuth < 180.0)
            azimuth += 360.0;
    }
    else if (m_flipRotation)
    {
        azimuth = std::fmod(azimuth + 180.0, 360.0);
        elevation = 180.0 - elevation;
    }

    QList<ObjectPipe*> rotatorPipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(m_satelliteTracker, "target", rotatorPipes);

    for (const auto& pipe : rotatorPipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);
        SWGSDRangel::SWGTargetAzimuthElevation *swgTarget = new SWGSDRangel::SWGTargetAzimuthElevation();
        swgTarget->setName(new QString(m_settings.m_target));
        swgTarget->setAzimuth(azimuth);
        swgTarget->setElevation(elevation);
        messageQueue->push(MainCore::MsgTargetAzimuthElevation::create(m_satelliteTracker, swgTarget));
    }
}

// Determine if we need to flip rotator or use extended azimuth to avoid 360/0 discontinuity
void SatelliteTrackerWorker::calculateRotation(SatWorkerState *satWorkerState)
{
    m_flipRotation = false;
    m_extendedAzRotation = false;
    if (satWorkerState->m_satState.m_passes.size() > 0)
    {
        SatNogsSatellite *sat = m_satellites.value(satWorkerState->m_name);
        bool passes0 = getPassesThrough0Deg(*sat->m_tle,
                                            m_settings.m_latitude, m_settings.m_longitude, m_settings.m_heightAboveSeaLevel/1000.0,
                                            satWorkerState->m_satState.m_passes[0].m_aos, satWorkerState->m_satState.m_passes[0].m_los);
        if (passes0)
        {
            double aosAz = satWorkerState->m_satState.m_passes[0].m_aosAzimuth;
            double losAz = satWorkerState->m_satState.m_passes[0].m_losAzimuth;
            double minAz = std::min(aosAz, losAz);
            if ((m_settings.m_rotatorMaxAzimuth - 360.0) > minAz)
                m_extendedAzRotation = true;
            else if (m_settings.m_rotatorMaxElevation == 180.0)
                m_flipRotation = true;
        }
    }
}

QString SatelliteTrackerWorker::substituteVariables(const QString &textIn, const QString &satelliteName)
{
    SatWorkerState *satWorkerState = m_workerState.value(satelliteName);
    if (!satWorkerState) {
        return "";
    }

    int durationMins = (int)round((satWorkerState->m_los.toSecsSinceEpoch() - satWorkerState->m_aos.toSecsSinceEpoch())/60.0);

    QString text = textIn;
    text = text.replace("${name}", satelliteName);
    text = text.replace("${duration}", QString::number(durationMins));
    if (satWorkerState->m_satState.m_passes.size() > 0)
    {
        text = text.replace("${aos}", satWorkerState->m_satState.m_passes[0].m_aos.toString());
        text = text.replace("${los}", satWorkerState->m_satState.m_passes[0].m_los.toString());
        text = text.replace("${elevation}", QString::number(std::round(satWorkerState->m_satState.m_passes[0].m_maxElevation)));
        text = text.replace("${aosAzimuth}", QString::number(std::round(satWorkerState->m_satState.m_passes[0].m_aosAzimuth)));
        text = text.replace("${losAzimuth}", QString::number(std::round(satWorkerState->m_satState.m_passes[0].m_losAzimuth)));
        text = text.replace("${northToSouth}", QString::number(satWorkerState->m_satState.m_passes[0].m_northToSouth));
        text = text.replace("${latitude}", QString::number(satWorkerState->m_satState.m_latitude));
        text = text.replace("${longitude}", QString::number(satWorkerState->m_satState.m_longitude));
        text = text.replace("${altitude}", QString::number(satWorkerState->m_satState.m_altitude));
        text = text.replace("${azimuth}", QString::number(std::round(satWorkerState->m_satState.m_azimuth)));
        text = text.replace("${elevation}", QString::number(std::round(satWorkerState->m_satState.m_elevation)));
        text = text.replace("${range}", QString::number(std::round(satWorkerState->m_satState.m_range)));
        text = text.replace("${rangeRate}", QString::number(std::round(satWorkerState->m_satState.m_rangeRate)));
        text = text.replace("${speed}", QString::number(std::round(satWorkerState->m_satState.m_speed)));
        text = text.replace("${period}", QString::number(satWorkerState->m_satState.m_period));
    }
    return text;
}

void SatelliteTrackerWorker::executeCommand(const QString &command, const QString &satelliteName)
{
    if (!command.isEmpty())
    {
#if QT_CONFIG(process)
        // Replace variables
        QString cmd = substituteVariables(command, satelliteName);
        QStringList allArgs = QProcess::splitCommand(cmd);
        qDebug() << "SatelliteTrackerWorker::executeCommand: Executing: " << allArgs;
        QString program = allArgs[0];
        allArgs.pop_front();
        QProcess::startDetached(program, allArgs);
#else
        qWarning() << "SatelliteTrackerWorker::executeCommand: QProcess not supported. Can't run: " << command;
#endif
    }
}

void SatelliteTrackerWorker::applyDeviceAOSSettings(const QString& name)
{
    // Execute global program/script
    if (!m_settings.m_aosCommand.isEmpty()) {
        executeCommand(m_settings.m_aosCommand, name);
    }

    // Update device set
    if (m_settings.m_deviceSettings.contains(name))
    {
        QList<SatelliteTrackerSettings::SatelliteDeviceSettings *> *m_deviceSettingsList = m_settings.m_deviceSettings.value(name);

        MainCore *mainCore = MainCore::instance();

        // Load presets
        for (int i = 0; i < m_deviceSettingsList->size(); i++)
        {
            SatelliteTrackerSettings::SatelliteDeviceSettings *devSettings = m_deviceSettingsList->at(i);
            if (!devSettings->m_presetGroup.isEmpty())
            {
                const MainSettings& mainSettings = mainCore->getSettings();
                const std::vector<DeviceSet*>& deviceSets = mainCore->getDeviceSets();

                if (devSettings->m_deviceSetIndex < (int)deviceSets.size())
                {
                    const DeviceSet *deviceSet = deviceSets[devSettings->m_deviceSetIndex];
                    QString presetType;
                    if (deviceSet->m_deviceSourceEngine != nullptr) {
                        presetType = "R";
                    } else if (deviceSet->m_deviceSinkEngine != nullptr) {
                        presetType = "T";
                    } else if (deviceSet->m_deviceMIMOEngine != nullptr) {
                        presetType = "M";
                    }

                    const Preset* preset = mainSettings.getPreset(devSettings->m_presetGroup, devSettings->m_presetFrequency, devSettings->m_presetDescription, presetType);

                    if (preset != nullptr)
                    {
                        qDebug() << "SatelliteTrackerWorker::aos: Loading preset " << preset->getDescription() << " to device set at " << devSettings->m_deviceSetIndex;
                        MainCore::MsgLoadPreset *msg = MainCore::MsgLoadPreset::create(preset, devSettings->m_deviceSetIndex);
                        mainCore->getMainMessageQueue()->push(msg);
                    }
                    else
                    {
                        qWarning() << "SatelliteTrackerWorker::aos: Unable to get preset: " << devSettings->m_presetGroup << " " << devSettings->m_presetFrequency << " " << devSettings->m_presetDescription;
                    }
                }
                else
                {
                    qWarning() << "SatelliteTrackerWorker::aos: device set at " << devSettings->m_deviceSetIndex << " does not exist";
                }
            }
        }

        // Wait a little bit for presets to load before performing other steps
        QTimer::singleShot(1000, [this, name, m_deviceSettingsList]()
        {

            for (int i = 0; i < m_deviceSettingsList->size(); i++)
            {
                SatelliteTrackerSettings::SatelliteDeviceSettings *devSettings = m_deviceSettingsList->at(i);

                // Override frequency
                if (devSettings->m_frequency != 0)
                {
                    qDebug() << "SatelliteTrackerWorker::aos: setting frequency to: " << devSettings->m_frequency;
                    ChannelWebAPIUtils::setCenterFrequency(devSettings->m_deviceSetIndex, devSettings->m_frequency);
                }

                // Execute per satellite program/script
                if (!devSettings->m_aosCommand.isEmpty()) {
                    executeCommand(devSettings->m_aosCommand, name);
                }

            }

            // Start acquisition - Need to use WebAPI, in order for GUI to correctly reflect being started
            for (int i = 0; i < m_deviceSettingsList->size(); i++)
            {
                SatelliteTrackerSettings::SatelliteDeviceSettings *devSettings = m_deviceSettingsList->at(i);
                if (devSettings->m_startOnAOS)
                {
                    qDebug() << "SatelliteTrackerWorker::aos: starting acquisition";
                    ChannelWebAPIUtils::run(devSettings->m_deviceSetIndex);
                }
            }

            // Send AOS message to channels/features
            SatWorkerState *satWorkerState = m_workerState.value(name);
            SatNogsSatellite *sat = m_satellites.value(satWorkerState->m_name);
            // APT needs current time, for current position of satellite, not start of pass which may be in the past
            // if the satellite was already visible when Sat Tracker was started
            ChannelWebAPIUtils::satelliteAOS(name, satWorkerState->m_satState.m_passes[0].m_northToSouth,
                                             sat->m_tle->toString(),
                                             m_satelliteTracker->currentDateTimeUtc());
            FeatureWebAPIUtils::satelliteAOS(name, satWorkerState->m_aos, satWorkerState->m_los);

            // Start Doppler correction, if needed
            enableDoppler(satWorkerState);

            // Start file sinks (need a little delay to ensure sample rate message has been handled in filerecord)
            QTimer::singleShot(1000, [m_deviceSettingsList]()
            {
                for (int i = 0; i < m_deviceSettingsList->size(); i++)
                {
                    SatelliteTrackerSettings::SatelliteDeviceSettings *devSettings = m_deviceSettingsList->at(i);

                    if (devSettings->m_startStopFileSink)
                    {
                        qDebug() << "SatelliteTrackerWorker::aos: starting file sinks";
                        ChannelWebAPIUtils::startStopFileSinks(devSettings->m_deviceSetIndex, true);
                    }
                }
            });

        });
    }
    else
    {
        // Send AOS message to channels/features
        SatWorkerState *satWorkerState = m_workerState.value(name);
        SatNogsSatellite *sat = m_satellites.value(satWorkerState->m_name);
        ChannelWebAPIUtils::satelliteAOS(name, satWorkerState->m_satState.m_passes[0].m_northToSouth,
                                            sat->m_tle->toString(),
                                            m_satelliteTracker->currentDateTimeUtc());
        FeatureWebAPIUtils::satelliteAOS(name, satWorkerState->m_aos, satWorkerState->m_los);
    }

}

void SatelliteTrackerWorker::enableDoppler(SatWorkerState *satWorkerState)
{
    QList<SatelliteTrackerSettings::SatelliteDeviceSettings *> *m_deviceSettingsList = m_settings.m_deviceSettings.value(satWorkerState->m_name);
    if (m_deviceSettingsList)
    {
        satWorkerState->m_doppler.clear();
        bool requiresDoppler = false;
        for (int i = 0; i < m_deviceSettingsList->size(); i++)
        {
            SatelliteTrackerSettings::SatelliteDeviceSettings *devSettings = m_deviceSettingsList->at(i);
            if (devSettings->m_doppler.size() > 0)
            {
                requiresDoppler = true;
                satWorkerState->m_doppler.append(0);
            }
        }
        if (requiresDoppler)
        {
            qDebug() << "SatelliteTrackerWorker::applyDeviceAOSSettings: Enabling doppler for " << satWorkerState->m_name;
            satWorkerState->m_dopplerTimer.setInterval(m_settings.m_dopplerPeriod * 1000);
            satWorkerState->m_dopplerTimer.start();
            disconnect(satWorkerState->m_connection);
            satWorkerState->m_connection = connect(&satWorkerState->m_dopplerTimer, &QTimer::timeout, [this, satWorkerState]() {
                doppler(satWorkerState);
            });
        }
    }
}

void SatelliteTrackerWorker::disableDoppler(SatWorkerState *satWorkerState)
{
    // Stop Doppler timer, and set interval to 0, so we don't restart it in start()
    satWorkerState->m_dopplerTimer.stop();
    satWorkerState->m_dopplerTimer.setInterval(0);
    disconnect(satWorkerState->m_connection);
    // Remove doppler correction from any channel
    if (satWorkerState->m_doppler.size() > 0)
    {
        QList<SatelliteTrackerSettings::SatelliteDeviceSettings *> *m_deviceSettingsList = m_settings.m_deviceSettings.value(satWorkerState->m_name);
        if (m_deviceSettingsList)
        {
            int idx = 0;

            for (int i = 0; i < m_deviceSettingsList->size(); i++)
            {
                SatelliteTrackerSettings::SatelliteDeviceSettings *devSettings = m_deviceSettingsList->at(i);
                if (devSettings->m_doppler.size() > 0)
                {
                    for (int j = 0; j < devSettings->m_doppler.size(); j++)
                    {
                        int offset;
                        if (ChannelWebAPIUtils::getFrequencyOffset(devSettings->m_deviceSetIndex, devSettings->m_doppler[j], offset))
                        {
                            // Remove old doppler
                            std::vector<DeviceSet*>& deviceSets = MainCore::instance()->getDeviceSets();
                            ChannelAPI *channel = deviceSets[devSettings->m_deviceSetIndex]->getChannelAt(j);
                            int tx = false;
                            if (channel) {
                                tx = channel->getStreamType() == ChannelAPI::StreamSingleSource; // What if MIMO?
                            }
                            if (tx) {
                                offset -= satWorkerState->m_doppler[idx];
                            } else {
                                offset += satWorkerState->m_doppler[idx];
                            }
                            if (!ChannelWebAPIUtils::setFrequencyOffset(devSettings->m_deviceSetIndex, devSettings->m_doppler[j], offset))
                                qDebug() << "SatelliteTrackerWorker::disableDoppler: Failed to set frequency offset";
                        }
                        else
                            qDebug() << "SatelliteTrackerWorker::disableDoppler: Failed to get frequency offset";
                    }
                    satWorkerState->m_doppler[idx] = 0;
                    idx++;
                }
            }
        }
    }
}

void SatelliteTrackerWorker::doppler(SatWorkerState *satWorkerState)
{
    qDebug() << "SatelliteTrackerWorker::doppler " << satWorkerState->m_name;

    QList<SatelliteTrackerSettings::SatelliteDeviceSettings *> *m_deviceSettingsList = m_settings.m_deviceSettings.value(satWorkerState->m_name);
    if (m_deviceSettingsList)
    {
        int idx = 0;

        for (int i = 0; i < m_deviceSettingsList->size(); i++)
        {
            SatelliteTrackerSettings::SatelliteDeviceSettings *devSettings = m_deviceSettingsList->at(i);
            if (devSettings->m_doppler.size() > 0)
            {
                // Get center frequency for this device
                double centerFrequency;

                if (ChannelWebAPIUtils::getCenterFrequency(devSettings->m_deviceSetIndex, centerFrequency))
                {
                    // Calculate frequency delta due to Doppler
                    double c = 299792458.0;
                    double deltaF = centerFrequency * satWorkerState->m_satState.m_rangeRate * 1000.0 / c;
                    int doppler = (int)round(deltaF);

                    for (int j = 0; j < devSettings->m_doppler.size(); j++)
                    {
                        int offset;
                        if (ChannelWebAPIUtils::getFrequencyOffset(devSettings->m_deviceSetIndex, devSettings->m_doppler[j], offset))
                        {
                            // Apply doppler - For receive, we subtract, transmit we add
                            std::vector<DeviceSet*>& deviceSets = MainCore::instance()->getDeviceSets();
                            ChannelAPI *channel = deviceSets[devSettings->m_deviceSetIndex]->getChannelAt(j);
                            int tx = false;
                            if (channel) {
                                tx = channel->getStreamType() == ChannelAPI::StreamSingleSource; // What if MIMO?
                            }

                            // Remove old doppler and apply new
                            int initOffset;
                            if (tx)
                            {
                                initOffset = offset - satWorkerState->m_doppler[idx];
                                offset = initOffset + doppler;
                            }
                            else
                            {
                                initOffset = offset + satWorkerState->m_doppler[idx];
                                offset = initOffset - doppler;
                            }
                            if (!ChannelWebAPIUtils::setFrequencyOffset(devSettings->m_deviceSetIndex, devSettings->m_doppler[j], offset))
                                qDebug() << "SatelliteTrackerWorker::doppler: Failed to set frequency offset";
                        }
                        else
                            qDebug() << "SatelliteTrackerWorker::doppler: Failed to get frequency offset";
                    }

                    satWorkerState->m_doppler[idx] = doppler;
                }
                else
                    qDebug() << "SatelliteTrackerWorker::doppler: couldn't get centre frequency for device at " << devSettings->m_deviceSetIndex;

                idx++;
            }
        }
    }
}

void SatelliteTrackerWorker::startTune(const QString& satellite, int deviceSetIndex, int channelIndex, qint64 frequency, bool fastDoppler, int tuneId,
    const QList<int>& companionChannelIndexes)
{
    qDebug() << "SatelliteTrackerWorker::startTune:" << satellite << deviceSetIndex << channelIndex << frequency << fastDoppler << companionChannelIndexes;

    m_tuneSatellite = satellite;
    m_tuneDeviceSetIndex = deviceSetIndex;
    m_tuneChannelIndex = channelIndex;
    m_tuneFrequency = frequency;
    m_tuneFastDoppler = fastDoppler;
    m_tuneId = tuneId;
    m_tuneLastFrequency = -1;
    m_tuneDeviceRetuned = false;
    m_tuneCompanionChannelIndexes = companionChannelIndexes;

    tuneDoppler();
    if (!m_tuneSatellite.isEmpty())
    {
        m_tuneTimer.setInterval(tuneInterval());
        m_tuneTimer.start();
    }
}

// Keep other channels on the device (E.g. a Packet demodulator for telemetry sent with SSTV) at the tracked channel's frequency
void SatelliteTrackerWorker::tuneCompanionChannels(int offset)
{
    for (int channelIndex : m_tuneCompanionChannelIndexes)
    {
        int companionOffset;
        if (ChannelWebAPIUtils::getFrequencyOffset(m_tuneDeviceSetIndex, channelIndex, companionOffset)
            && (companionOffset != offset)
            && !ChannelWebAPIUtils::setFrequencyOffset(m_tuneDeviceSetIndex, channelIndex, offset))
        {
            qDebug() << "SatelliteTrackerWorker::tuneCompanionChannels: Failed to set frequency offset of channel" << channelIndex;
        }
    }
}

// Doppler on a LEO satellite can change by ~100Hz/s on 70cm, which is too much
// for SSB and CW with the default Doppler period, so update at least every second
int SatelliteTrackerWorker::tuneInterval() const
{
    float period = m_tuneFastDoppler ? std::min(m_settings.m_dopplerPeriod, 1.0f) : m_settings.m_dopplerPeriod;
    return (int) round(period * 1000.0);
}

void SatelliteTrackerWorker::stopTune()
{
    qDebug() << "SatelliteTrackerWorker::stopTune:" << m_tuneSatellite;

    // Leave channel at last Doppler corrected frequency
    m_tuneTimer.stop();
    m_tuneSatellite.clear();
}

// Set channel so that it receives the satellite transmitter frequency, corrected for Doppler.
// Uses the channel offset where possible, only retuning the device when the signal would
// fall outside of the baseband.
void SatelliteTrackerWorker::tuneDoppler()
{
    SatWorkerState *satWorkerState = m_workerState.value(m_tuneSatellite);
    std::vector<DeviceSet*>& deviceSets = MainCore::instance()->getDeviceSets();

    if (!satWorkerState
        || (m_tuneDeviceSetIndex < 0)
        || (m_tuneDeviceSetIndex >= (int) deviceSets.size())
        || !deviceSets[m_tuneDeviceSetIndex]->getChannelAt(m_tuneChannelIndex))
    {
        qDebug() << "SatelliteTrackerWorker::tuneDoppler: Satellite or channel no longer available - stopping";
        stopTune();
        return;
    }

    DeviceSet *deviceSet = deviceSets[m_tuneDeviceSetIndex];
    int streamIndex = deviceSet->getChannelAt(m_tuneChannelIndex)->getStreamIndex();
    double centerFrequency;
    int sampleRate;
    int offset;
    if (!getRxCenterFrequency(deviceSet, streamIndex, centerFrequency, sampleRate)
        || !ChannelWebAPIUtils::getFrequencyOffset(m_tuneDeviceSetIndex, m_tuneChannelIndex, offset))
    {
        qDebug() << "SatelliteTrackerWorker::tuneDoppler: Failed to get device or channel frequency";
        return;
    }

    // If channel's frequency differs from what we last set it to, the user has retuned it (E.g. to a different part
    // of a transponder's passband), so move the frequency we're tracking by the same amount
    qint64 currentFrequency = (qint64) round(centerFrequency) + offset;
    if (m_tuneDeviceRetuned)
    {
        // Device may round the center frequency we set, so don't treat that as user retuning
        m_tuneDeviceRetuned = false;
    }
    else if ((m_tuneLastFrequency >= 0) && (currentFrequency != m_tuneLastFrequency))
    {
        qDebug() << "SatelliteTrackerWorker::tuneDoppler: Channel retuned by" << (currentFrequency - m_tuneLastFrequency) << "Hz";
        m_tuneFrequency += currentFrequency - m_tuneLastFrequency;
    }

    // Received frequency is lower as range increases (positive range rate)
    const double c = 299792458.0;
    double doppler = m_tuneFrequency * satWorkerState->m_satState.m_rangeRate * 1000.0 / c;
    qint64 rxFrequency = m_tuneFrequency - (qint64) round(doppler);

    qint64 newOffset = rxFrequency - (qint64) round(centerFrequency);

    if ((sampleRate > 0) && (std::abs(newOffset) < sampleRate * 0.45))
    {
        if (newOffset != offset)
        {
            if (!ChannelWebAPIUtils::setFrequencyOffset(m_tuneDeviceSetIndex, m_tuneChannelIndex, (int) newOffset)) {
                qDebug() << "SatelliteTrackerWorker::tuneDoppler: Failed to set frequency offset";
            }
        }
        tuneCompanionChannels((int) newOffset);
    }
    else
    {
        // Out of band - retune device, keeping channel's current offset from centre (avoiding DC)
        if ((sampleRate > 0) && (std::abs(offset) >= sampleRate * 0.45)) {
            offset = 0;
        }
        if (!setRxCenterFrequency(deviceSet, streamIndex, rxFrequency - offset)) {
            qDebug() << "SatelliteTrackerWorker::tuneDoppler: Failed to set center frequency";
        }
        if (!ChannelWebAPIUtils::setFrequencyOffset(m_tuneDeviceSetIndex, m_tuneChannelIndex, offset)) {
            qDebug() << "SatelliteTrackerWorker::tuneDoppler: Failed to set frequency offset";
        }
        tuneCompanionChannels(offset);
        m_tuneDeviceRetuned = true;
    }
    m_tuneLastFrequency = rxFrequency;

    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(SatelliteTrackerReport::MsgReportTune::create(m_tuneId, m_tuneFrequency));
    }
}

// Get center frequency and baseband sample rate for an Rx stream of a single Rx or MIMO device
bool SatelliteTrackerWorker::getRxCenterFrequency(DeviceSet *deviceSet, int streamIndex, double& centerFrequency, int& sampleRate)
{
    if (deviceSet->m_deviceSourceEngine)
    {
        DeviceSampleSource *source = deviceSet->m_deviceAPI->getSampleSource();
        sampleRate = source ? source->getSampleRate() : 0;
        return ChannelWebAPIUtils::getCenterFrequency(m_tuneDeviceSetIndex, centerFrequency);
    }
    else if (deviceSet->m_deviceMIMOEngine)
    {
        DeviceSampleMIMO *mimo = deviceSet->m_deviceAPI->getSampleMIMO();
        if (!mimo) {
            return false;
        }
        sampleRate = mimo->getSourceSampleRate(streamIndex);
        centerFrequency = mimo->getSourceCenterFrequency(streamIndex);
        return true;
    }
    return false;
}

bool SatelliteTrackerWorker::setRxCenterFrequency(DeviceSet *deviceSet, int streamIndex, qint64 centerFrequency)
{
    if (deviceSet->m_deviceSourceEngine)
    {
        return ChannelWebAPIUtils::setCenterFrequency(m_tuneDeviceSetIndex, centerFrequency);
    }
    else if (deviceSet->m_deviceMIMOEngine)
    {
        DeviceSampleMIMO *mimo = deviceSet->m_deviceAPI->getSampleMIMO();
        if (!mimo) {
            return false;
        }
        mimo->setSourceCenterFrequency(centerFrequency, streamIndex);
        return true;
    }
    return false;
}

void SatelliteTrackerWorker::los(SatWorkerState *satWorkerState)
{
    qDebug() << "SatelliteTrackerWorker::los " << satWorkerState->m_name << " target: " << m_settings.m_target;

    // Indicate LOS to GUI
    if (getMessageQueueToGUI())
    {
        QString speech = substituteVariables(m_settings.m_losSpeech, satWorkerState->m_name);
        getMessageQueueToGUI()->push(SatelliteTrackerReport::MsgReportLOS::create(satWorkerState->m_name, speech));
    }

    disableDoppler(satWorkerState);

    if (m_settings.m_target == satWorkerState->m_name)
    {
        // Execute program/script
        if (!m_settings.m_losCommand.isEmpty()) {
            executeCommand(m_settings.m_losCommand, satWorkerState->m_name);
        }

        // Send LOS message to channels/features
        ChannelWebAPIUtils::satelliteLOS(satWorkerState->m_name);
        FeatureWebAPIUtils::satelliteLOS(satWorkerState->m_name);

        if (m_settings.m_deviceSettings.contains(satWorkerState->m_name))
        {
            QList<SatelliteTrackerSettings::SatelliteDeviceSettings *> *m_deviceSettingsList = m_settings.m_deviceSettings.value(satWorkerState->m_name);

            // Stop file sinks
            for (int i = 0; i < m_deviceSettingsList->size(); i++)
            {
                SatelliteTrackerSettings::SatelliteDeviceSettings *devSettings = m_deviceSettingsList->at(i);
                if (devSettings->m_startStopFileSink)
                {
                    qDebug() << "SatelliteTrackerWorker::los: stopping file sinks";
                    ChannelWebAPIUtils::startStopFileSinks(devSettings->m_deviceSetIndex, false);
                }
            }

            // Stop acquisition
            for (int i = 0; i < m_deviceSettingsList->size(); i++)
            {
                SatelliteTrackerSettings::SatelliteDeviceSettings *devSettings = m_deviceSettingsList->at(i);

                if (devSettings->m_stopOnLOS) {
                    ChannelWebAPIUtils::stop(devSettings->m_deviceSetIndex);
                }
            }

            // Execute per satellite program/script
            // Do after stopping acquisition, so files are closed by file sink
            for (int i = 0; i < m_deviceSettingsList->size(); i++)
            {
                SatelliteTrackerSettings::SatelliteDeviceSettings *devSettings = m_deviceSettingsList->at(i);
                if (!devSettings->m_losCommand.isEmpty()) {
                    executeCommand(devSettings->m_losCommand, satWorkerState->m_name);
                }
            }
        }
    }

    // Is another lower-priority satellite with AOS available to switch to?
    if (m_settings.m_autoTarget)
    {
        for (int i = m_settings.m_satellites.indexOf(m_settings.m_target) + 1; i < m_settings.m_satellites.size(); i++)
        {
            if (m_workerState.contains(m_settings.m_satellites[i]))
            {
                SatWorkerState *newSatWorkerState = m_workerState.value(m_settings.m_satellites[i]);
                if (newSatWorkerState->hasAOS(m_satelliteTracker->currentDateTimeUtc()))
                {
                    qDebug() << "SatelliteTrackerWorker::los - autoTarget setting " << m_settings.m_satellites[i];
                    m_settings.m_target = m_settings.m_satellites[i];
                    // Update GUI with new target
                    if (getMessageQueueToGUI())
                        getMessageQueueToGUI()->push(SatelliteTrackerReport::MsgReportTarget::create(m_settings.m_target));
                    // Apply device settings
                    applyDeviceAOSSettings(m_settings.m_target);
                    break;
                }
            }
        }
    }

    m_recalculatePasses = true;
}

bool SatWorkerState::hasAOS(const QDateTime& currentTime)
{
    return (m_aos <= currentTime) && (m_los > currentTime);
}
