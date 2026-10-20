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

#ifndef INCLUDE_SSTVDEMODSINK_H
#define INCLUDE_SSTVDEMODSINK_H

#include "dsp/channelsamplesink.h"
#include "dsp/phasediscri.h"
#include "dsp/nco.h"
#include "dsp/interpolator.h"
#include "dsp/fftfilt.h"
#include "dsp/firfilter.h"
#include "audio/audiofifo.h"
#include "util/messagequeue.h"

#include "sstvdecoder.h"
#include "sstvdemodsettings.h"

class ChannelAPI;

class SSTVDemodSink : public ChannelSampleSink, public SSTVDecoder::Listener
{
public:
    SSTVDemodSink();

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
    void applySettings(const QStringList& settingsKeys, const SSTVDemodSettings& settings, bool force = false);
    void setMessageQueueToChannel(MessageQueue *messageQueue) { m_messageQueueToChannel = messageQueue; }
    void resetDecoder() { m_decoder.reset(); }

    AudioFifo *getAudioFifo() { return &m_audioFifo; }
    void setAudioFifoLabel(const QString& label) { m_audioFifo.setLabel(label); }
    void applyAudioSampleRate(int sampleRate);
    int getAudioSampleRate() const { return m_audioSampleRate; }
    void setChannel(ChannelAPI *channel) { m_channel = channel; }

    // SSTVDecoder::Listener
    void imageStarted(int modeIndex, int visCode) override;
    void linesDecoded(const QImage& image, int firstRow, int rowCount) override;
    void imageFinished(const QImage& image, bool complete) override;

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

    static const unsigned m_fftFilterLength;
    static const double m_audioCutoff;

    int m_channelSampleRate;
    int m_channelFrequencyOffset;
    SSTVDemodSettings m_settings;
    ChannelAPI *m_channel;

    int m_audioSampleRate;
    AudioVector m_audioBuffer;
    std::size_t m_audioBufferFill;
    AudioFifo m_audioFifo;

    NCO m_nco;
    Interpolator m_interpolator;
    Real m_interpolatorDistance;
    Real m_interpolatorDistanceRemain;
    fftfilt m_rfFilter;
    Lowpass<Real> m_audioLowpass;
    PhaseDiscriminators m_phaseDiscri;
    SSTVDecoder m_decoder;

    double m_magsq;
    double m_magsqSum;
    double m_magsqPeak;
    int m_magsqCount;
    MagSqLevelsStore m_magSqLevelStore;

    MessageQueue *m_messageQueueToChannel;

    void processOneSample(Complex& ci);
    void createRFFilter();
};

#endif // INCLUDE_SSTVDEMODSINK_H
