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

#ifndef INCLUDE_SSTVDECODER_H
#define INCLUDE_SSTVDECODER_H

#include <complex>
#include <vector>

#include <QImage>

#include "sstvmodes.h"

// Decodes SSTV images from demodulated audio.
//
// The audio is mixed down around 1900 Hz and low-pass filtered to give an analytic
// signal at about 12 kHz. The product of each sample with the conjugate of the previous
// one has an argument proportional to the instantaneous tone frequency; summing these
// products over a pixel, a VIS bit or a sync pulse and taking the argument of the sum
// gives a frequency estimate that is weighted by signal amplitude and robust to noise.
//
// The VIS code selects the mode (unless a mode is forced) and the start of the image.
// Each sync pulse is then searched for near its predicted position, and a line is fitted
// through the pulses found, so the image follows the transmitter's line rate (slant
// correction) and missing pulses are bridged.
class SSTVDecoder
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        //! visCode is -1 when the image was started from sync pulses rather than a VIS code
        virtual void imageStarted(int modeIndex, int visCode) = 0;
        virtual void linesDecoded(const QImage& image, int firstRow, int rowCount) = 0;
        virtual void imageFinished(const QImage& image, bool complete) = 0;
    };

    SSTVDecoder();

    void setListener(Listener *listener) { m_listener = listener; }
    void setSampleRate(int sampleRate);
    //! Mode index from SSTVMode, or -1 to select the mode from the VIS code
    void setMode(int modeIndex);
    //! Abandon any image being received and wait for the next one
    void reset();
    void process(float sample);

    bool isReceiving() const { return m_receiving; }
    int getModeIndex() const { return m_modeIndex; }
    int getRowsDone() const { return m_rowsDone; }
    double getClockErrorPpm() const;
    double getToneFrequency() const { return m_toneFrequency; } //!< Average tone frequency of the last 30 ms

private:
    typedef std::complex<float> cfloat;

    struct Tick
    {
        cfloat m_sum;           //!< Sum of the frequency products over the tick
        float m_magnitude;      //!< Sum of their magnitudes
        qint64 m_endSample;     //!< Absolute index of the sample after the tick
    };

    struct FIR
    {
        std::vector<float> m_taps;
        std::vector<cfloat> m_delay; //!< Doubled delay line so the taps can be applied without wrapping
        int m_index;

        void create(int nbTaps, double sampleRate, double cutoff);
        void push(const cfloat& sample);
        cfloat output() const;
    };

    Listener *m_listener;
    int m_sampleRate;
    int m_decimation;
    double m_rate;              //!< Decimated sample rate
    int m_forcedMode;           //!< -1 for automatic selection from VIS code

    // Front end
    double m_mixPhase;
    double m_mixIncrement;
    int m_decimationCount;
    FIR m_fir1;
    FIR m_fir2;
    cfloat m_previous;

    // Buffer of frequency products, indexed by absolute sample number
    std::vector<cfloat> m_buffer;
    qint64 m_bufferStart;
    qint64 m_sampleCount;       //!< Number of products received

    // 1 ms ticks for VIS and sync detection
    static const int m_nbTicks = 1024;
    static const int m_maxRedrawPeriods = 32; //!< Lines kept to redraw once the line rate is known
    Tick m_ticks[m_nbTicks];
    qint64 m_tickCount;
    int m_tickLength;
    int m_tickFill;
    cfloat m_tickSum;
    float m_tickMagnitude;
    double m_toneFrequency;
    double m_coherence;         //!< Average tone coherence over the ticks: low when there is only noise

    // VIS detection
    bool m_visCandidate;
    qint64 m_visCandidateTick;
    double m_visCandidateScore;
    int m_visCandidateCode;
    qint64 m_visCandidateEnd;
    qint64 m_visHoldoffTick;    //!< No VIS detection before this tick

    // Sync detection when a mode is forced and no VIS code was received
    int m_syncRunTicks;
    qint64 m_syncRunStart;
    std::vector<qint64> m_syncCandidates;

    // Image reception
    bool m_receiving;
    int m_modeIndex;
    int m_visCode;
    QImage m_image;
    int m_syncIndex;            //!< Next sync period to decode
    int m_rowsDone;
    double m_nominalPeriod;     //!< In samples
    double m_firstSync;         //!< Estimated position of the first sync pulse
    double m_origin;            //!< Initial estimate of the first sync, the origin of the fit
    double m_period;            //!< Estimated sync period
    bool m_fitEstablished;      //!< Line rate measured from the sync pulses
    int m_fitCount;
    double m_fitSumK;
    double m_fitSumKK;
    double m_fitSumM;
    double m_fitSumKM;
    int m_fitFirstK;
    int m_fitLastK;
    double m_minOffsetMs;       //!< Earliest segment start relative to its sync pulse
    double m_maxEndMs;          //!< Latest segment end relative to its sync pulse
    double m_searchWindow;      //!< Sync search window either side of the prediction, in samples
    int m_missedSyncs;
    qint64 m_lastSyncSample;
    std::vector<float> m_comp[4]; //!< Pixel values of each segment of the current period
    std::vector<float> m_prevY; //!< Robot 36: luminance of the previous line
    std::vector<float> m_lastRY; //!< Robot 36: last R-Y received
    std::vector<float> m_lastBY; //!< Robot 36: last B-Y received
    bool m_prevHadRY;

    void processDecimated(const cfloat& z);
    void processTick();
    void checkVIS();
    void checkSync(const Tick& tick);
    bool tickWindow(double from, double to, double& frequency, double& coherence) const;
    double toneFrequency(const cfloat& sum) const;

    void startImage(int modeIndex, int visCode, double firstSync);
    void finishImage(bool complete);
    void decodeAvailable();
    bool findSync(double predicted, double& position, double& quality) const;
    bool addSync(int k, double position);
    void decodePeriod(int k);
    void renderPeriod(int k, int& firstRow, int& rowCount);
    void samplePixels(double start, double duration, int count, std::vector<float>& values) const;
    void renderRowYUV(int row, const std::vector<float>& y, const std::vector<float>& ry, const std::vector<float>& by);
    void renderRowRGB(int row, const std::vector<float>& r, const std::vector<float>& g, const std::vector<float>& b);
    float syncLevel(qint64 sample) const;
    cfloat bufferAt(qint64 sample) const;
    void trimBuffer(qint64 keepFrom);
};

#endif // INCLUDE_SSTVDECODER_H
