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

#ifndef INCLUDE_VARAMODSOURCE_H
#define INCLUDE_VARAMODSOURCE_H

#include <memory>
#include <vector>

#include <QMutex>

#include "dsp/channelsamplesource.h"
#include "dsp/nco.h"
#include "dsp/interpolator.h"
#include "dsp/fftfilt.h"

#include "varamodsettings.h"

class ChannelAPI;
class VARATxLink;

// Modulates the 48 kHz real audio of a VARA modem, as a radio with a data port would:
// flat FM in the FM modes, USB in the HF modes
class VARAModSource : public ChannelSampleSource
{
public:
    VARAModSource();
    ~VARAModSource() final;

    void pull(SampleVector::iterator begin, unsigned int nbSamples) final;
    void pullOne(Sample& sample) final;
    void prefetch(unsigned int nbSamples) final { (void) nbSamples; }

    void setTxLink(std::shared_ptr<VARATxLink> link); //!< Any thread
    void setChannel(ChannelAPI *channel) { m_channel = channel; }
    double getMagSq() const { return m_magsq; }
    void getLevels(qreal& rmsLevel, qreal& peakLevel, int& numSamples) const
    {
        rmsLevel = m_rmsLevel;
        peakLevel = m_peakLevelOut;
        numSamples = m_levelNbSamples;
    }

    void applySettings(const QStringList& settingsKeys, const VARAModSettings& settings, bool force = false);
    void applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force = false);

private:
    static const unsigned m_ssbFftLen;
    static const int m_levelNbSamples;

    int m_channelSampleRate;
    int m_channelFrequencyOffset;
    VARAModSettings m_settings;
    ChannelAPI *m_channel;

    QMutex m_linkMutex;
    std::shared_ptr<VARATxLink> m_txLink;
    std::vector<float> m_modemBuffer;
    int m_modemBufferPos;
    bool m_fm; //!< Mode of the engine feeding the link

    NCO m_carrierNco;
    Interpolator m_interpolator;
    Real m_interpolatorDistance;
    Real m_interpolatorDistanceRemain;
    Complex m_modSample;
    float m_modPhasor;
    Real m_gain;
    fftfilt *m_ssbFilter;
    std::vector<Complex> m_ssbBuffer;
    int m_ssbBufferPos;

    double m_magsq;
    quint32 m_levelCalcCount;
    qreal m_rmsLevel;
    qreal m_peakLevelOut;
    Real m_peakLevel;
    Real m_levelSum;

    float nextModemSample();
    void modulateSample();
    void calculateLevel(Real sample);
};

#endif // INCLUDE_VARAMODSOURCE_H
