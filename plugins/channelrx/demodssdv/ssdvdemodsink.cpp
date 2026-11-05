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

#include <QDebug>

#include "dsp/scopevis.h"

#include "ssdvdemodreport.h"
#include "ssdvdemodsink.h"

SSDVDemodSink::SSDVDemodSink() :
    m_channelSampleRate(SSDVDemodSettings::m_samplesPerSymbol * 9600),
    m_channelFrequencyOffset(0),
    m_symbolSampleRate(SSDVDemodSettings::m_samplesPerSymbol * 9600),
    m_channel(nullptr),
    m_interpolatorDistance(1.0f),
    m_interpolatorDistanceRemain(1.0f),
    m_magsq(0.0),
    m_magsqSum(0.0),
    m_magsqPeak(0.0),
    m_magsqCount(0),
    m_scopeSink(nullptr),
    m_scopeBufferIndex(0),
    m_messageQueueToChannel(nullptr)
{
    m_scopeBuffer.resize(m_scopeBufferSize);
    applySettings(QStringList(), m_settings, true);
    applyChannelSettings(m_channelSampleRate, m_channelFrequencyOffset, true);
}

CCSDS::CodingSettings SSDVDemodSink::getCodingSettings(const SSDVDemodSettings& settings)
{
    CCSDS::CodingSettings coding;
    coding.m_frameSize = std::max(1, std::min(settings.m_rs ? CCSDS::m_rsMaxData : 1024, settings.m_frameSize));
    coding.m_rs = settings.m_rs;
    coding.m_dualBasis = settings.m_dualBasis;
    coding.m_randomizer = settings.m_randomizer;
    coding.m_differential = settings.m_differential;
    coding.m_convolutional = settings.m_convolutional;
    return coding;
}

void SSDVDemodSink::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end)
{
    if (m_channelSampleRate == 0) {
        return;
    }

    for (SampleVector::const_iterator it = begin; it != end; ++it)
    {
        Complex c(it->real(), it->imag());
        c *= m_nco.nextIQ();
        Complex ci;

        if (m_interpolatorDistance == 1.0f)
        {
            processOneSample(c);
        }
        else if (m_interpolatorDistance < 1.0f) // interpolate
        {
            while (!m_interpolator.interpolate(&m_interpolatorDistanceRemain, c, &ci))
            {
                processOneSample(ci);
                m_interpolatorDistanceRemain += m_interpolatorDistance;
            }
        }
        else // decimate
        {
            if (m_interpolator.decimate(&m_interpolatorDistanceRemain, c, &ci))
            {
                processOneSample(ci);
                m_interpolatorDistanceRemain += m_interpolatorDistance;
            }
        }
    }
}

void SSDVDemodSink::processOneSample(Complex& ci)
{
    Real magsq = std::norm(ci) / (SDR_RX_SCALED*SDR_RX_SCALED);
    m_magsqSum += magsq;
    m_magsqPeak = std::max<double>(magsq, m_magsqPeak);
    m_magsqCount++;

    Complex symbol;

    if (!m_demod.process(ci / SDR_RX_SCALEF, symbol)) {
        return;
    }

    sampleToScope(symbol);

    QList<CCSDS::DecodedFrame> frames;
    m_frameDecoder.process(symbol.real(), frames);

    if (m_messageQueueToChannel)
    {
        QDateTime now = QDateTime::currentDateTime();

        for (const auto& frame : frames) {
            m_messageQueueToChannel->push(SSDVDemodReport::MsgFrame::create(frame.m_data, frame.m_rsErrors, frame.m_asmErrors, now));
        }
    }
}

void SSDVDemodSink::sampleToScope(const Complex& symbol)
{
    if (!m_scopeSink) {
        return;
    }

    m_scopeBuffer[m_scopeBufferIndex++] = Sample(symbol.real() * SDR_RX_SCALEF / 2.0f, symbol.imag() * SDR_RX_SCALEF / 2.0f);

    if (m_scopeBufferIndex == m_scopeBufferSize)
    {
        std::vector<SampleVector::const_iterator> vbegin;
        vbegin.push_back(m_scopeBuffer.begin());
        m_scopeSink->feed(vbegin, m_scopeBufferSize);
        m_scopeBufferIndex = 0;
    }
}

void SSDVDemodSink::createInterpolator()
{
    // Pass the RRC spectrum when off by up to the AFC's range of half the symbol rate.
    // The RRC filter after the AFC does the channel filtering.
    Real cutoff = m_settings.m_baudRate * (1.0f + m_settings.m_rrcRolloff) / 2.0f + m_settings.m_baudRate / 2.0f;
    m_interpolator.create(16, m_channelSampleRate, std::min(cutoff, 0.45f * std::min(m_channelSampleRate, m_symbolSampleRate)));
    m_interpolatorDistance = Real(m_channelSampleRate) / Real(m_symbolSampleRate);
    m_interpolatorDistanceRemain = m_interpolatorDistance;
}

void SSDVDemodSink::applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force)
{
    qDebug() << "SSDVDemodSink::applyChannelSettings:"
            << " channelSampleRate: " << channelSampleRate
            << " channelFrequencyOffset: " << channelFrequencyOffset;

    const bool rateChanged = (channelSampleRate != m_channelSampleRate) || force;

    if ((channelFrequencyOffset != m_channelFrequencyOffset) || rateChanged) {
        m_nco.setFreq(-channelFrequencyOffset, channelSampleRate);
    }

    m_channelSampleRate = channelSampleRate;
    m_channelFrequencyOffset = channelFrequencyOffset;

    if (rateChanged) {
        createInterpolator();
    }
}

void SSDVDemodSink::applySettings(const QStringList& settingsKeys, const SSDVDemodSettings& settings, bool force)
{
    qDebug() << "SSDVDemodSink::applySettings:" << settings.getDebugString(settingsKeys, force);

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    if (settingsKeys.contains("baudRate") || settingsKeys.contains("rrcRolloff") || force)
    {
        m_symbolSampleRate = m_settings.m_baudRate * SSDVDemodSettings::m_samplesPerSymbol;
        createInterpolator();
    }

    if (settingsKeys.contains("baudRate") || settingsKeys.contains("rrcRolloff")
        || settingsKeys.contains("pllBandwidth") || settingsKeys.contains("afc") || force)
    {
        m_demod.init(SSDVDemodSettings::m_samplesPerSymbol, m_settings.m_rrcRolloff, m_settings.m_pllBandwidth, m_settings.m_afc);
    }

    m_frameDecoder.setSettings(getCodingSettings(m_settings));
}
