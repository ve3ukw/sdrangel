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

#ifndef INCLUDE_VARADEMODSINK_H
#define INCLUDE_VARADEMODSINK_H

#include <vector>

#include "dsp/channelsamplesink.h"
#include "dsp/phasediscri.h"
#include "dsp/nco.h"
#include "dsp/interpolator.h"
#include "dsp/fftfilt.h"
#include "audio/audiofifo.h"

#include "varademodsettings.h"

class ChannelAPI;
class VARAEngine;

// Turns the channel into the 48 kHz real audio the VARA modem expects, as from a radio's data port:
// flat FM discriminator output in the FM modes, USB in the HF modes
class VARADemodSink : public ChannelSampleSink
{
public:
    VARADemodSink();

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

    void getModemLevels(float& rms, float& peak); //!< Of the modem input, since the last call
    void applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force = false);
    void applySettings(const QStringList& settingsKeys, const VARADemodSettings& settings, bool force = false);
    void setEngine(VARAEngine *engine) { m_engine = engine; }

    AudioFifo *getAudioFifo() { return &m_audioFifo; }
    void setAudioFifoLabel(const QString& label) { m_audioFifo.setLabel(label); }
    void applyAudioSampleRate(int sampleRate);
    int getAudioSampleRate() const { return m_audioSampleRate; }
    void setChannel(ChannelAPI *channel) { m_channel = channel; }

    static Real getRFBandwidth(int mode); //!< Of the channel filter
    static Real getHFLowCutoff() { return m_hfLowCutoff; }
    static Real getHFHighCutoff() { return m_hfHighCutoff; }

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
    static const Real m_hfLowCutoff;
    static const Real m_hfHighCutoff;
    static const Real m_agcTarget;

    int m_channelSampleRate;
    int m_channelFrequencyOffset;
    VARADemodSettings m_settings;
    ChannelAPI *m_channel;
    VARAEngine *m_engine;

    NCO m_nco;
    fftfilt m_rfFilter;
    Interpolator m_interpolator;
    Real m_interpolatorDistance;
    Real m_interpolatorDistanceRemain;
    PhaseDiscriminators m_phaseDiscri;
    Real m_rxGain;
    Real m_agcPower;

    std::vector<float> m_modemBuffer;
    int m_modemBufferFill;
    double m_modemLevelSum;
    float m_modemLevelPeak;
    int m_modemLevelCount;

    int m_audioSampleRate;
    Interpolator m_audioInterpolator;
    Real m_audioInterpolatorDistance;
    Real m_audioInterpolatorDistanceRemain;
    AudioVector m_audioBuffer;
    std::size_t m_audioBufferFill;
    AudioFifo m_audioFifo;

    double m_magsq;
    double m_magsqSum;
    double m_magsqPeak;
    int m_magsqCount;
    MagSqLevelsStore m_magSqLevelStore;

    void processOneSample(const Complex& ci);
    void pushAudio(Real sample);
    void writeAudio(Real sample);
    void createFilters();
};

#endif // INCLUDE_VARADEMODSINK_H
