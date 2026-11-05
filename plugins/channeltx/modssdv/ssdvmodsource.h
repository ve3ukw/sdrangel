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

#ifndef INCLUDE_SSDVMODSOURCE_H
#define INCLUDE_SSDVMODSOURCE_H

#include <vector>

#include "dsp/channelsamplesource.h"
#include "dsp/nco.h"
#include "dsp/interpolator.h"
#include "dsp/rootraisedcosine.h"
#include "util/movingaverage.h"
#include "util/messagequeue.h"

#include "ssdvmodsettings.h"

class ChannelAPI;

class SSDVModSource : public ChannelSampleSource
{
public:
    SSDVModSource();
    virtual ~SSDVModSource();

    virtual void pull(SampleVector::iterator begin, unsigned int nbSamples);
    virtual void pullOne(Sample& sample);
    virtual void prefetch(unsigned int nbSamples) { (void) nbSamples; }

    double getMagSq() const { return m_magsq; }
    void getLevels(qreal& rmsLevel, qreal& peakLevel, int& numSamples) const
    {
        rmsLevel = m_rmsLevel;
        peakLevel = m_peakLevelOut;
        numSamples = m_levelNbSamples;
    }
    void setMessageQueueToChannel(MessageQueue* messageQueue) { m_messageQueueToChannel = messageQueue; }
    void applySettings(const QStringList& settingsKeys, const SSDVModSettings& settings, bool force = false);
    void applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force = false);
    void setChannel(ChannelAPI *channel) { m_channel = channel; }
    int getChannelSampleRate() const { return m_channelSampleRate; }

    // Symbols are 0 or 1. progress is for each symbol index that ends a frame, the packets sent by then
    void transmit(const std::vector<uint8_t>& symbols, const std::vector<std::pair<int, int>>& progress);
    void abort();
    bool isTransmitting() const { return m_transmitting; }

private:
    static const int m_rrcSpan = 8;               //!< Symbols
    static const int m_levelNbSamples = 480;

    int m_channelSampleRate;
    int m_channelFrequencyOffset;
    int m_modSampleRate;                          //!< Samples per symbol times the baud rate
    SSDVModSettings m_settings;
    ChannelAPI *m_channel;

    NCO m_carrierNco;
    Real m_linearGain;
    Complex m_modSample;
    RootRaisedCosine<Real> m_pulseShape;

    Interpolator m_interpolator;
    Real m_interpolatorDistance;
    Real m_interpolatorDistanceRemain;

    std::vector<uint8_t> m_symbols;
    std::vector<std::pair<int, int>> m_progress;
    bool m_transmitting;
    int m_symbolIndex;
    int m_progressIndex;
    int m_sampleIdx;                              //!< Sample within the symbol
    int m_tail;                                   //!< Samples left to flush the pulse shaping filter

    double m_magsq;
    MovingAverageUtil<double, double, 16> m_movingAverage;
    quint32 m_levelCalcCount;
    qreal m_rmsLevel;
    qreal m_peakLevelOut;
    Real m_peakLevel;
    Real m_levelSum;

    MessageQueue* m_messageQueueToChannel;

    void createInterpolator();
    void modulateSample();
    void calculateLevel(Real sample);
};

#endif // INCLUDE_SSDVMODSOURCE_H
