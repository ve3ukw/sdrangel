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

#include "sstvdecoder.h"

#include <algorithm>
#include <cmath>

#include <QDebug>

namespace {

const double m_centerFrequency = 1900.0;     //!< Mixing frequency: the leader tone
const double m_blackFrequency = 1500.0;
const double m_whiteFrequency = 2300.0;
const double m_syncFrequency = 1200.0;
const double m_internalRate = 12000.0;       //!< Approximate rate after decimation
const double m_videoCutoff = 1100.0;         //!< Low-pass cutoff around the mixing frequency
const double m_visBitMs = 30.0;
const double m_historySeconds = 4.0;         //!< Kept while waiting for an image

}

void SSTVDecoder::FIR::create(int nbTaps, double sampleRate, double cutoff)
{
    nbTaps |= 1;
    m_taps.resize(nbTaps);
    const int middle = nbTaps / 2;
    double sum = 0.0;

    for (int i = 0; i < nbTaps; i++)
    {
        const double n = i - middle;
        const double x = 2.0 * cutoff / sampleRate;
        const double sinc = (n == 0) ? x : std::sin(M_PI * x * n) / (M_PI * n);
        const double window = 0.42 - 0.5 * std::cos(2.0 * M_PI * i / (nbTaps - 1)) + 0.08 * std::cos(4.0 * M_PI * i / (nbTaps - 1));
        m_taps[i] = sinc * window;
        sum += m_taps[i];
    }

    for (auto& tap : m_taps) {
        tap /= sum;
    }

    m_delay.assign(2 * nbTaps, cfloat(0.0f, 0.0f));
    m_index = 0;
}

void SSTVDecoder::FIR::push(const cfloat& sample)
{
    const int size = (int) m_taps.size();
    m_delay[m_index] = sample;
    m_delay[m_index + size] = sample;
    m_index = (m_index + 1) % size;
}

SSTVDecoder::cfloat SSTVDecoder::FIR::output() const
{
    const int size = (int) m_taps.size();
    const cfloat *delay = &m_delay[m_index];
    float re = 0.0f;
    float im = 0.0f;

    for (int i = 0; i < size; i++)
    {
        re += m_taps[i] * delay[i].real();
        im += m_taps[i] * delay[i].imag();
    }

    return cfloat(re, im);
}

SSTVDecoder::SSTVDecoder() :
    m_listener(nullptr),
    m_sampleRate(0),
    m_forcedMode(-1),
    m_receiving(false),
    m_modeIndex(-1),
    m_visCode(-1),
    m_rowsDone(0)
{
    setSampleRate(48000);
}

void SSTVDecoder::setSampleRate(int sampleRate)
{
    if ((sampleRate <= 0) || (sampleRate == m_sampleRate)) {
        return;
    }

    m_sampleRate = sampleRate;
    m_decimation = std::max(1, (int) std::lround(sampleRate / m_internalRate));
    m_rate = sampleRate / (double) m_decimation;
    m_mixIncrement = 2.0 * M_PI * m_centerFrequency / sampleRate;
    // First stage only has to reject what would alias onto the video band after decimation
    m_fir1.create((int) std::lround(sampleRate / 1000.0), sampleRate, std::min(3000.0, m_rate / 2.0 - m_videoCutoff));
    m_fir2.create((int) std::lround(m_rate * 0.008), m_rate, m_videoCutoff);
    m_tickLength = std::max(1, (int) std::lround(m_rate / 1000.0));
    reset();
}

void SSTVDecoder::setMode(int modeIndex)
{
    m_forcedMode = ((modeIndex >= 0) && (modeIndex < SSTVMode::NbModes)) ? modeIndex : -1;
    m_syncCandidates.clear();
    m_syncRunTicks = 0;
}

void SSTVDecoder::reset()
{
    m_mixPhase = 0.0;
    m_decimationCount = 0;
    m_previous = cfloat(0.0f, 0.0f);
    m_buffer.clear();
    m_bufferStart = 0;
    m_sampleCount = 0;
    m_tickCount = 0;
    m_tickFill = 0;
    m_tickSum = cfloat(0.0f, 0.0f);
    m_tickMagnitude = 0.0f;
    m_toneFrequency = 0.0;
    m_coherence = 0.0;
    m_visCandidate = false;
    m_visHoldoffTick = 0;
    m_syncRunTicks = 0;
    m_syncRunStart = 0;
    m_syncCandidates.clear();
    m_receiving = false;
}

double SSTVDecoder::getClockErrorPpm() const
{
    if (!m_receiving || (m_nominalPeriod <= 0.0)) {
        return 0.0;
    }

    return (m_period / m_nominalPeriod - 1.0) * 1e6;
}

void SSTVDecoder::process(float sample)
{
    const cfloat mixed(sample * std::cos(m_mixPhase), -sample * std::sin(m_mixPhase));
    m_mixPhase += m_mixIncrement;

    if (m_mixPhase > 2.0 * M_PI) {
        m_mixPhase -= 2.0 * M_PI;
    }

    m_fir1.push(mixed);

    if (++m_decimationCount >= m_decimation)
    {
        m_decimationCount = 0;
        processDecimated(m_fir1.output());
    }
}

void SSTVDecoder::processDecimated(const cfloat& z)
{
    m_fir2.push(z);
    const cfloat y = m_fir2.output();
    const cfloat d = y * std::conj(m_previous);
    m_previous = y;

    m_buffer.push_back(d);
    m_sampleCount++;

    m_tickSum += d;
    m_tickMagnitude += std::abs(d);

    if (++m_tickFill >= m_tickLength) {
        processTick();
    }

    if (m_receiving)
    {
        decodeAvailable();
    }
    else if ((m_sampleCount - m_bufferStart) > 2 * m_historySeconds * m_rate)
    {
        trimBuffer(m_sampleCount - (qint64) (m_historySeconds * m_rate));
    }
}

SSTVDecoder::cfloat SSTVDecoder::bufferAt(qint64 sample) const
{
    if ((sample < m_bufferStart) || (sample >= m_sampleCount)) {
        return cfloat(0.0f, 0.0f);
    }

    return m_buffer[sample - m_bufferStart];
}

void SSTVDecoder::trimBuffer(qint64 keepFrom)
{
    if (keepFrom <= m_bufferStart) {
        return;
    }

    keepFrom = std::min(keepFrom, m_sampleCount);
    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + (keepFrom - m_bufferStart));
    m_bufferStart = keepFrom;
}

double SSTVDecoder::toneFrequency(const cfloat& sum) const
{
    return m_centerFrequency + std::atan2(sum.imag(), sum.real()) * m_rate / (2.0 * M_PI);
}

void SSTVDecoder::processTick()
{
    Tick& tick = m_ticks[m_tickCount % m_nbTicks];
    tick.m_sum = m_tickSum;
    tick.m_magnitude = m_tickMagnitude;
    tick.m_endSample = m_sampleCount;
    m_tickCount++;
    m_tickFill = 0;
    m_tickSum = cfloat(0.0f, 0.0f);
    m_tickMagnitude = 0.0f;

    const double coherence = tick.m_magnitude > 0.0f ? std::abs(tick.m_sum) / tick.m_magnitude : 0.0;
    m_coherence += (coherence - m_coherence) * 0.002; // About 0.5 s time constant
    double frequency;
    double windowCoherence;

    if (tickWindow(m_tickCount - 30.0, m_tickCount, frequency, windowCoherence)) {
        m_toneFrequency = frequency;
    }

    checkVIS();

    if (!m_receiving && (m_forcedMode >= 0)) {
        checkSync(tick);
    }
}

// Sum ticks in [from, to), given as absolute tick numbers
bool SSTVDecoder::tickWindow(double from, double to, double& frequency, double& coherence) const
{
    const qint64 first = std::lround(from);
    const qint64 last = std::lround(to);

    if ((last <= first) || (first < 0) || (first < m_tickCount - m_nbTicks) || (last > m_tickCount)) {
        return false;
    }

    cfloat sum(0.0f, 0.0f);
    float magnitude = 0.0f;

    for (qint64 i = first; i < last; i++)
    {
        sum += m_ticks[i % m_nbTicks].m_sum;
        magnitude += m_ticks[i % m_nbTicks].m_magnitude;
    }

    frequency = toneFrequency(sum);
    coherence = magnitude > 0.0f ? std::abs(sum) / magnitude : 0.0;
    return true;
}

// VIS code: 300 ms leader at 1900 Hz, 10 ms break at 1200 Hz, 300 ms leader,
// then 30 ms bits: start bit (1200 Hz), 7 data bits LSB first and an even
// parity bit (1100 Hz for 1, 1300 Hz for 0), and a stop bit (1200 Hz).
// This checks whether the stop bit has just ended.
void SSTVDecoder::checkVIS()
{
    const double tickMs = m_tickLength * 1000.0 / m_rate;
    const double bit = m_visBitMs / tickMs;
    const double edge = 5.0 / tickMs;
    const double end = m_tickCount;
    const double startBit = end - 10.0 * bit;
    bool match = true;
    double score = 0.0;
    int code = 0;
    double frequency;
    double coherence;

    // Leader, checked in four parts so a slow sweep does not match
    for (int i = 0; (i < 4) && match; i++)
    {
        const double from = startBit - (5.0 + 50.0 * (4 - i)) / tickMs;

        if (!tickWindow(from, from + 50.0 / tickMs, frequency, coherence)
            || (coherence < 0.6) || (std::abs(frequency - m_centerFrequency) > 60.0)) {
            match = false;
        } else {
            score += (frequency - m_centerFrequency) * (frequency - m_centerFrequency);
        }
    }

    // Start and stop bits
    for (double from : {startBit, end - bit})
    {
        if (!match) {
            break;
        }

        if (!tickWindow(from + edge, from + bit - edge, frequency, coherence)
            || (coherence < 0.5) || (std::abs(frequency - m_syncFrequency) > 60.0)) {
            match = false;
        } else {
            score += (frequency - m_syncFrequency) * (frequency - m_syncFrequency);
        }
    }

    int ones = 0;

    for (int i = 0; (i < 8) && match; i++)
    {
        const double from = startBit + (i + 1) * bit;

        if (!tickWindow(from + edge, from + bit - edge, frequency, coherence) || (coherence < 0.4))
        {
            match = false;
        }
        else if (std::abs(frequency - 1100.0) < 80.0)
        {
            score += (frequency - 1100.0) * (frequency - 1100.0);

            if (i < 7) {
                code |= 1 << i;
            }

            ones++;
        }
        else if (std::abs(frequency - 1300.0) < 80.0)
        {
            score += (frequency - 1300.0) * (frequency - 1300.0);
        }
        else
        {
            match = false;
        }
    }

    if (match && ((ones % 2) != 0)) { // Even parity
        match = false;
    }

    // The same VIS code also matches a few ms after it was accepted
    if (m_tickCount < m_visHoldoffTick) {
        match = false;
    }

    if (match && (!m_visCandidate || (score < m_visCandidateScore)))
    {
        m_visCandidate = true;
        m_visCandidateTick = m_tickCount;
        m_visCandidateScore = score;
        m_visCandidateCode = code;
        m_visCandidateEnd = m_ticks[(m_tickCount - 1) % m_nbTicks].m_endSample;
    }

    // Take the best aligned match once the pattern has passed
    if (m_visCandidate && (m_tickCount - m_visCandidateTick >= 10))
    {
        m_visCandidate = false;
        m_visHoldoffTick = m_tickCount + (qint64) (500.0 / tickMs);
        const int visMode = SSTVMode::getModeIndexFromVIS(m_visCandidateCode);
        const int modeIndex = m_forcedMode >= 0 ? m_forcedMode : visMode;

        qDebug("SSTVDecoder::checkVIS: VIS code %d (%s)", m_visCandidateCode,
            visMode >= 0 ? SSTVMode::getMode(visMode).m_name : "unsupported");

        if (modeIndex >= 0)
        {
            if (m_receiving) {
                finishImage(false);
            }

            const SSTVMode& mode = SSTVMode::getMode(modeIndex);
            startImage(modeIndex, m_visCandidateCode, m_visCandidateEnd + mode.m_firstSyncMs * m_rate / 1000.0);
        }
    }
}

// With a forced mode, start an image without a VIS code when two sync pulses
// of the mode's length are found a whole number of periods apart
void SSTVDecoder::checkSync(const Tick& tick)
{
    const SSTVMode& mode = SSTVMode::getMode(m_forcedMode);
    const double tickMs = m_tickLength * 1000.0 / m_rate;
    const double frequency = toneFrequency(tick.m_sum);
    const double coherence = tick.m_magnitude > 0.0f ? std::abs(tick.m_sum) / tick.m_magnitude : 0.0;

    if ((coherence > 0.5) && (std::abs(frequency - m_syncFrequency) < 100.0))
    {
        if (m_syncRunTicks == 0) {
            m_syncRunStart = tick.m_endSample - m_tickLength;
        }

        m_syncRunTicks++;
        return;
    }

    if (m_syncRunTicks == 0) {
        return;
    }

    const double runMs = m_syncRunTicks * tickMs;
    m_syncRunTicks = 0;

    if ((runMs < std::max(3.0, 0.6 * mode.m_syncMs)) || (runMs > 1.5 * mode.m_syncMs + 2.0)) {
        return;
    }

    const double period = mode.m_periodMs * m_rate / 1000.0;
    const double tolerance = (3.0 + 0.01 * mode.m_periodMs) * m_rate / 1000.0;

    for (qint64 previous : m_syncCandidates)
    {
        for (int n = 1; n <= 3; n++)
        {
            if (std::abs(m_syncRunStart - previous - n * period) < tolerance)
            {
                m_syncCandidates.clear();
                startImage(m_forcedMode, -1, previous);
                return;
            }
        }
    }

    m_syncCandidates.push_back(m_syncRunStart);
    m_syncCandidates.erase(
        std::remove_if(m_syncCandidates.begin(), m_syncCandidates.end(), [this, period](qint64 position) {
            return m_syncRunStart - position > 4.0 * period;
        }),
        m_syncCandidates.end());
}

void SSTVDecoder::startImage(int modeIndex, int visCode, double firstSync)
{
    const SSTVMode& mode = SSTVMode::getMode(modeIndex);

    qDebug("SSTVDecoder::startImage: %s first sync at sample %.1f", mode.m_name, firstSync);

    m_receiving = true;
    m_modeIndex = modeIndex;
    m_visCode = visCode;
    m_image = QImage(mode.m_width, mode.m_height, QImage::Format_RGB32);
    m_image.fill(Qt::black);
    m_syncIndex = 0;
    m_rowsDone = 0;
    m_nominalPeriod = mode.m_periodMs * m_rate / 1000.0;
    m_period = m_nominalPeriod;
    m_firstSync = firstSync;
    m_origin = firstSync;
    m_fitEstablished = false;
    m_fitCount = 0;
    m_fitSumK = 0.0;
    m_fitSumKK = 0.0;
    m_fitSumM = 0.0;
    m_fitSumKM = 0.0;
    m_fitFirstK = 0;
    m_fitLastK = 0;
    m_missedSyncs = 0;
    m_lastSyncSample = (qint64) firstSync;
    m_prevY.clear();
    m_lastRY.clear();
    m_lastBY.clear();
    m_prevHadRY = false;
    m_minOffsetMs = 0.0;
    m_maxEndMs = mode.m_syncMs;

    for (int i = 0; i < mode.m_nbSegments; i++)
    {
        m_minOffsetMs = std::min(m_minOffsetMs, mode.m_segments[i].m_offsetMs);
        m_maxEndMs = std::max(m_maxEndMs, mode.m_segments[i].m_offsetMs + mode.m_segments[i].m_durationMs);
    }

    m_searchWindow = (4.0 + 0.005 * mode.m_periodMs) * m_rate / 1000.0;

    if (m_listener) {
        m_listener->imageStarted(modeIndex, visCode);
    }

    decodeAvailable();
}

void SSTVDecoder::finishImage(bool complete)
{
    if (!m_receiving) {
        return;
    }

    qDebug("SSTVDecoder::finishImage: %s %s, %d rows, clock %.0f ppm",
        SSTVMode::getMode(m_modeIndex).m_name, complete ? "complete" : "incomplete", m_rowsDone, getClockErrorPpm());

    m_receiving = false;
    m_syncCandidates.clear();
    m_syncRunTicks = 0;

    if (m_listener) {
        m_listener->imageFinished(m_image, complete);
    }
}

void SSTVDecoder::decodeAvailable()
{
    const SSTVMode& mode = SSTVMode::getMode(m_modeIndex);
    const double guard = m_rate / 1000.0 + 2.0;

    while (m_receiving)
    {
        if (m_syncIndex >= mode.getSyncCount())
        {
            finishImage(true);
            break;
        }

        const double predicted = m_firstSync + m_syncIndex * m_period;
        const double msToSamples = (m_rate / 1000.0) * (m_period / m_nominalPeriod);

        if (predicted + m_maxEndMs * msToSamples + m_searchWindow + guard >= m_sampleCount) {
            break;
        }

        decodePeriod(m_syncIndex);
        m_syncIndex++;

        // Give up when the signal has gone
        if ((m_missedSyncs >= 8) && (m_coherence < 0.45) && (m_sampleCount - m_lastSyncSample > 5.0 * m_rate)) {
            finishImage(false);
        }
    }

    if (m_receiving)
    {
        const double msToSamples = (m_rate / 1000.0) * (m_period / m_nominalPeriod);
        // Until the line rate is known, keep the start of the image to redraw it
        const int firstKept = (m_fitEstablished || (m_syncIndex > m_maxRedrawPeriods)) ? m_syncIndex : 0;
        const double needed = m_firstSync + firstKept * m_period + m_minOffsetMs * msToSamples - m_searchWindow - 2.0 * guard;

        if (needed - m_bufferStart > m_rate) {
            trimBuffer((qint64) needed);
        }
    }
}

float SSTVDecoder::syncLevel(qint64 sample) const
{
    const double frequency = toneFrequency(bufferAt(sample));
    // +1 at the sync frequency, -1 at black and above
    const double level = ((m_syncFrequency + m_blackFrequency) / 2.0 - frequency) / 150.0;
    return (float) std::max(-1.0, std::min(1.0, level));
}

// Find the start of the sync pulse near a predicted position, as the position where the
// pulse is best surrounded by non-sync tones
bool SSTVDecoder::findSync(double predicted, double& position, double& quality) const
{
    const SSTVMode& mode = SSTVMode::getMode(m_modeIndex);
    const int length = std::max(1, (int) std::lround(mode.m_syncMs * m_rate / 1000.0));
    const int guard = std::max(2, (int) std::lround(m_rate / 1000.0));
    const qint64 first = (qint64) std::floor(predicted - m_searchWindow);
    const qint64 last = (qint64) std::ceil(predicted + m_searchWindow);
    const qint64 from = first - guard;
    const qint64 to = last + length + 2 * guard + 2;
    std::vector<float> prefix(to - from + 1);

    prefix[0] = 0.0f;

    for (qint64 n = from; n < to; n++) {
        prefix[n - from + 1] = prefix[n - from] + syncLevel(n);
    }

    const auto sum = [&prefix, from](qint64 a, qint64 b) { return prefix[b - from] - prefix[a - from]; };
    double best = -10.0;
    qint64 bestPosition = first;

    for (qint64 x = first; x <= last; x++)
    {
        const double inside = sum(x, x + length) / length;
        const double outside = (sum(x - guard, x) + sum(x + length, x + length + guard)) / (2.0 * guard);
        const double score = inside - outside;

        if (score > best)
        {
            best = score;
            bestPosition = x;
        }
    }

    quality = best;

    if ((best < 1.0) || (sum(bestPosition, bestPosition + length) / length < 0.3)) {
        return false;
    }

    // The end of the pulse is always a step from 1200 Hz to the 1500 Hz porch, whereas the
    // start depends on the video before it, so time the pulse from its end
    qint64 edge = bestPosition + length;
    double bestEdge = -10.0;

    for (qint64 y = bestPosition + length - guard; y <= bestPosition + length + guard; y++)
    {
        const double step = sum(y - guard, y) - sum(y, y + guard);

        if (step > bestEdge)
        {
            bestEdge = step;
            edge = y;
        }
    }

    // Interpolate where the level crosses zero, taking each sample at the middle of its interval
    double crossing = edge;

    for (qint64 n = edge - 1; n <= edge + 1; n++)
    {
        const double before = syncLevel(n - 1);
        const double after = syncLevel(n);

        if ((before > 0.0) && (after <= 0.0))
        {
            crossing = (n - 0.5) + before / (before - after);
            break;
        }
    }

    position = crossing - mode.m_syncMs * m_rate / 1000.0;
    return true;
}

bool SSTVDecoder::addSync(int k, double position)
{
    const double predicted = m_firstSync + k * m_period;
    const double tolerance = m_searchWindow / 2.0;

    if ((m_fitCount >= 4) && (std::abs(position - predicted) > tolerance)) {
        return false;
    }

    const double m = position - m_origin;

    if (m_fitCount == 0) {
        m_fitFirstK = k;
    }

    m_fitCount++;
    m_fitLastK = k;
    m_fitSumK += k;
    m_fitSumKK += (double) k * k;
    m_fitSumM += m;
    m_fitSumKM += k * m;
    const double n = m_fitCount;

    if ((m_fitCount >= 8) && (m_fitLastK - m_fitFirstK >= 8))
    {
        const double denominator = n * m_fitSumKK - m_fitSumK * m_fitSumK;
        const double period = (n * m_fitSumKM - m_fitSumK * m_fitSumM) / denominator;

        if (std::abs(period / m_nominalPeriod - 1.0) <= 0.01)
        {
            m_fitEstablished = true;
            m_period = period;
            m_firstSync = m_origin + (m_fitSumM - period * m_fitSumK) / n;
            return true;
        }

        m_period = m_nominalPeriod;
    }

    m_firstSync = m_origin + (m_fitSumM - m_period * m_fitSumK) / n;
    return true;
}

void SSTVDecoder::samplePixels(double start, double duration, int count, std::vector<float>& values) const
{
    values.resize(count);
    const double width = duration / count;
    const double scale = 255.0 / (m_whiteFrequency - m_blackFrequency);

    for (int i = 0; i < count; i++)
    {
        const double x0 = start + i * width;
        const double x1 = x0 + width;
        cfloat sum(0.0f, 0.0f);

        for (qint64 n = (qint64) std::floor(x0); n < x1; n++)
        {
            const double overlap = std::min(x1, (double) (n + 1)) - std::max(x0, (double) n);

            if (overlap > 0.0) {
                sum += (float) overlap * bufferAt(n);
            }
        }

        const double value = (toneFrequency(sum) - m_blackFrequency) * scale;
        values[i] = (float) std::max(0.0, std::min(255.0, value));
    }
}

void SSTVDecoder::decodePeriod(int k)
{
    const SSTVMode& mode = SSTVMode::getMode(m_modeIndex);
    double position;
    double quality;

    const bool fitEstablished = m_fitEstablished;

    if (findSync(m_firstSync + k * m_period, position, quality) && addSync(k, position))
    {
        m_missedSyncs = 0;
        m_lastSyncSample = (qint64) position;
    }
    else
    {
        m_missedSyncs++;
    }

    m_rowsDone = (k + 1) * mode.m_linesPerSync;

    if (fitEstablished || !m_fitEstablished || (k > m_maxRedrawPeriods))
    {
        int firstRow;
        int rowCount;
        renderPeriod(k, firstRow, rowCount);

        if (m_listener) {
            m_listener->linesDecoded(m_image, firstRow, rowCount);
        }
    }
    else
    {
        // The line rate is now known: redraw the lines drawn with the nominal rate
        m_prevY.clear();
        m_lastRY.clear();
        m_lastBY.clear();
        m_prevHadRY = false;
        int firstRow;
        int rowCount;

        for (int i = 0; i <= k; i++) {
            renderPeriod(i, firstRow, rowCount);
        }

        if (m_listener) {
            m_listener->linesDecoded(m_image, 0, firstRow + rowCount);
        }
    }
}

void SSTVDecoder::renderPeriod(int k, int& firstRow, int& rowCount)
{
    const SSTVMode& mode = SSTVMode::getMode(m_modeIndex);
    const double start = m_firstSync + k * m_period;
    const double msToSamples = (m_rate / 1000.0) * (m_period / m_nominalPeriod);

    for (int i = 0; i < mode.m_nbSegments; i++)
    {
        const SSTVMode::Segment& segment = mode.m_segments[i];
        samplePixels(start + segment.m_offsetMs * msToSamples, segment.m_durationMs * msToSamples, mode.m_width, m_comp[i]);
    }

    const auto component = [&mode, this](SSTVMode::Component c) -> const std::vector<float>& {
        for (int i = 0; i < mode.m_nbSegments; i++)
        {
            if (mode.m_segments[i].m_component == c) {
                return m_comp[i];
            }
        }
        return m_comp[0];
    };

    firstRow = k * mode.m_linesPerSync;
    rowCount = mode.m_linesPerSync;

    switch (mode.m_colorFormat)
    {
    case SSTVMode::RGB:
        renderRowRGB(k, component(SSTVMode::CompR), component(SSTVMode::CompG), component(SSTVMode::CompB));
        break;
    case SSTVMode::YUV422:
        renderRowYUV(k, component(SSTVMode::CompY), component(SSTVMode::CompRY), component(SSTVMode::CompBY));
        break;
    case SSTVMode::YUVDual:
        renderRowYUV(2 * k, component(SSTVMode::CompY), component(SSTVMode::CompRY), component(SSTVMode::CompBY));
        renderRowYUV(2 * k + 1, component(SSTVMode::CompY1), component(SSTVMode::CompRY), component(SSTVMode::CompBY));
        break;
    case SSTVMode::YUV420:
    {
        // The separator before the chrominance is 1500 Hz for R-Y and 2300 Hz for B-Y
        const std::vector<float>& y = component(SSTVMode::CompY);
        const std::vector<float>& chroma = component(SSTVMode::CompChroma);
        const double separatorStart = start + 100.75 * msToSamples;
        const double separatorEnd = start + 103.75 * msToSamples;
        cfloat sum(0.0f, 0.0f);

        for (qint64 n = (qint64) std::ceil(separatorStart); n < separatorEnd; n++) {
            sum += bufferAt(n);
        }

        const double separator = toneFrequency(sum);
        bool isRY = (k % 2) == 0;

        if (std::abs(separator - m_blackFrequency) < 200.0) {
            isRY = true;
        } else if (std::abs(separator - m_whiteFrequency) < 200.0) {
            isRY = false;
        }

        const std::vector<float> neutral(mode.m_width, 128.0f);

        if (isRY)
        {
            m_lastRY = chroma;
            renderRowYUV(k, y, chroma, m_lastBY.empty() ? neutral : m_lastBY);
            m_prevHadRY = true;
        }
        else
        {
            m_lastBY = chroma;
            renderRowYUV(k, y, m_lastRY.empty() ? neutral : m_lastRY, chroma);

            if (m_prevHadRY && (k > 0) && !m_prevY.empty())
            {
                renderRowYUV(k - 1, m_prevY, m_lastRY, chroma);
                firstRow--;
                rowCount++;
            }

            m_prevHadRY = false;
        }

        m_prevY = y;
        break;
    }
    }
}

void SSTVDecoder::renderRowYUV(int row, const std::vector<float>& y, const std::vector<float>& ry, const std::vector<float>& by)
{
    if ((row < 0) || (row >= m_image.height())) {
        return;
    }

    QRgb *line = reinterpret_cast<QRgb *>(m_image.scanLine(row));
    const int width = m_image.width();

    for (int i = 0; i < width; i++)
    {
        // ITU-R BT.601 with studio range levels
        const double luma = 298.082 * (y[i] - 16.0);
        const double cr = ry[i] - 128.0;
        const double cb = by[i] - 128.0;
        const int r = (int) std::lround((luma + 408.583 * cr) / 256.0);
        const int g = (int) std::lround((luma - 100.291 * cb - 208.120 * cr) / 256.0);
        const int b = (int) std::lround((luma + 516.411 * cb) / 256.0);
        line[i] = qRgb(std::max(0, std::min(255, r)), std::max(0, std::min(255, g)), std::max(0, std::min(255, b)));
    }
}

void SSTVDecoder::renderRowRGB(int row, const std::vector<float>& r, const std::vector<float>& g, const std::vector<float>& b)
{
    if ((row < 0) || (row >= m_image.height())) {
        return;
    }

    QRgb *line = reinterpret_cast<QRgb *>(m_image.scanLine(row));
    const int width = m_image.width();

    for (int i = 0; i < width; i++) {
        line[i] = qRgb((int) std::lround(r[i]), (int) std::lround(g[i]), (int) std::lround(b[i]));
    }
}
