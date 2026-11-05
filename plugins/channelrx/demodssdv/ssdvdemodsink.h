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

#ifndef INCLUDE_SSDVDEMODSINK_H
#define INCLUDE_SSDVDEMODSINK_H

#include "dsp/channelsamplesink.h"
#include "dsp/nco.h"
#include "dsp/interpolator.h"
#include "util/ccsds.h"
#include "util/messagequeue.h"

#include "ssdvbpskdemod.h"
#include "ssdvdemodsettings.h"

class ChannelAPI;
class ScopeVis;

class SSDVDemodSink : public ChannelSampleSink
{
public:
    SSDVDemodSink();

    void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end) override;

    void getMagSqLevels(double& avg, double& peak, int& nbSamples)
    {
        if (m_magsqCount > 0)
        {
            m_magsq = m_magsqSum / m_magsqCount;
            m_magSqLevelStore.m_magsq = m_magsq;
            m_magSqLevelStore.m_magsqPeak = m_magsqPeak;
        }

        avg = m_magSqLevelStore.m_magsq;
        peak = m_magSqLevelStore.m_magsqPeak;
        nbSamples = m_magsqCount == 0 ? 1 : m_magsqCount;

        m_magsqSum = 0.0f;
        m_magsqPeak = 0.0f;
        m_magsqCount = 0;
    }

    void applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force = false);
    void applySettings(const QStringList& settingsKeys, const SSDVDemodSettings& settings, bool force = false);
    void setMessageQueueToChannel(MessageQueue *messageQueue) { m_messageQueueToChannel = messageQueue; }
    void setChannel(ChannelAPI *channel) { m_channel = channel; }
    void setScopeSink(ScopeVis *scopeSink) { m_scopeSink = scopeSink; }

    bool isLocked() const { return m_demod.isLocked(); }
    float getFrequencyOffset() const { return m_demod.getFrequencyOffset() * m_symbolSampleRate; } //!< Hz
    float getEvm() const { return m_demod.getEvm(); }
    int getFramesBad() const { return m_frameDecoder.getFramesBad(); }

    static CCSDS::CodingSettings getCodingSettings(const SSDVDemodSettings& settings);

private:
    struct MagSqLevelsStore
    {
        MagSqLevelsStore() :
            m_magsq(1e-12),
            m_magsqPeak(1e-12)
        {}
        double m_magsq;
        double m_magsqPeak;
    };

    static const int m_scopeBufferSize = 128;

    int m_channelSampleRate;
    int m_channelFrequencyOffset;
    int m_symbolSampleRate; //!< Samples per symbol times the baud rate
    SSDVDemodSettings m_settings;
    ChannelAPI *m_channel;

    NCO m_nco;
    Interpolator m_interpolator;
    Real m_interpolatorDistance;
    Real m_interpolatorDistanceRemain;

    SSDVBPSKDemod m_demod;
    CCSDS::FrameDecoder m_frameDecoder;

    double m_magsq;
    double m_magsqSum;
    double m_magsqPeak;
    int m_magsqCount;
    MagSqLevelsStore m_magSqLevelStore;

    ScopeVis *m_scopeSink;
    SampleVector m_scopeBuffer;
    int m_scopeBufferIndex;

    MessageQueue *m_messageQueueToChannel;

    void processOneSample(Complex& ci);
    void sampleToScope(const Complex& symbol);
    void createInterpolator();
};

#endif // INCLUDE_SSDVDEMODSINK_H
