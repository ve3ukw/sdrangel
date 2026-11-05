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

// Runs a recording through the SSDV Demod sink, printing the frames and packets received:
//   ssdv_iqfile [options] file
// file is a WAV file (16 bit I/Q, or 16 bit / float mono audio with -a or -u), or raw complex float32 I/Q
// (raw float32 mono audio with -a or -u). FM audio, as SatNOGS records for FSK transmitters, is turned back into I/Q.
// Exits with 0 if a frame was received.

#include <cstdio>
#include <cmath>
#include <vector>

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QMap>
#include <QtEndian>

#include "util/messagequeue.h"
#include "util/ssdvcodec.h"

#include "ssdvdemodsink.h"
#include "ssdvdemodreport.h"
#include "ssdvdemodframeparser.h"

// Reads samples as I/Q pairs (or audio, then Q is 0), and the sample rate if the file has a header
static bool readFile(const QString& filename, bool audio, std::vector<std::complex<float>>& samples, int& rate)
{
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray data = file.readAll();

    if (data.startsWith("RIFF") && (data.mid(8, 4) == "WAVE"))
    {
        int format = 1, channels = 2, bits = 16;
        int pos = 12;

        while (pos + 8 <= data.size())
        {
            QByteArray id = data.mid(pos, 4);
            quint32 size = qFromLittleEndian<quint32>((const uchar *) data.constData() + pos + 4);
            const uchar *p = (const uchar *) data.constData() + pos + 8;

            if (id == "fmt ")
            {
                format = qFromLittleEndian<quint16>(p);
                channels = qFromLittleEndian<quint16>(p + 2);
                rate = qFromLittleEndian<quint32>(p + 4);
                bits = qFromLittleEndian<quint16>(p + 14);
            }
            else if (id == "data")
            {
                int bytesPerValue = bits / 8;
                int count = std::min<qint64>(size, data.size() - pos - 8) / (bytesPerValue * channels);

                for (int i = 0; i < count; i++)
                {
                    float v[2] = {0.0f, 0.0f};

                    for (int c = 0; c < std::min(channels, 2); c++)
                    {
                        const uchar *q = p + (i * channels + c) * bytesPerValue;
                        v[c] = (format == 3) && (bits == 32) ? qFromLittleEndian<float>(q) : qFromLittleEndian<qint16>(q) / 32768.0f;
                    }

                    samples.push_back({v[0], audio ? 0.0f : v[1]});
                }

                return true;
            }

            pos += 8 + size + (size & 1);
        }

        return false;
    }

    const float *f = (const float *) data.constData();

    if (audio)
    {
        for (int i = 0; i < (int) (data.size() / sizeof(float)); i++) {
            samples.push_back({f[i], 0.0f});
        }
    }
    else
    {
        for (int i = 0; i < (int) (data.size() / (2 * sizeof(float))); i++) {
            samples.push_back({f[2 * i], f[2 * i + 1]});
        }
    }

    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addPositionalArgument("file", "Recording");
    parser.addOption({"r", "Sample rate of raw files", "rate", "48000"});
    parser.addOption({"c", "Frequency of the signal in the recording in Hz", "offset", "0"});
    parser.addOption({"a", "File is FM audio"});
    parser.addOption({"u", "File is SSB audio (use -c for the signal's audio frequency)"});
    parser.addOption({"m", "Modulation (0 BPSK, 1 FSK)", "modulation", "0"});
    parser.addOption({"b", "Baud rate", "baud", "9600"});
    parser.addOption({"d", "FSK deviation in Hz", "deviation", "2400"});
    parser.addOption({"F", "Framing (0 CCSDS, 1 USP, 2 GENESIS)", "framing", "0"});
    parser.addOption({"f", "Packet format (0 AO-123, 1 standard)", "format", "0"});
    parser.addOption({"w", "Write received images as <prefix>_<image>.jpg", "prefix"});
    parser.addOption({"q", "Don't print each frame"});
    parser.process(app);

    if (parser.positionalArguments().size() != 1) {
        parser.showHelp(1);
    }

    bool audio = parser.isSet("a") || parser.isSet("u");
    int rate = parser.value("r").toInt();
    std::vector<std::complex<float>> samples;

    if (!readFile(parser.positionalArguments()[0], audio, samples, rate) || samples.empty())
    {
        fprintf(stderr, "could not read %s\n", qPrintable(parser.positionalArguments()[0]));
        return 2;
    }

    SSDVDemodSettings settings;
    settings.m_modulation = parser.value("m").toInt();
    settings.m_baudRate = parser.value("b").toInt();
    settings.m_fskDeviation = parser.value("d").toInt();
    settings.m_framing = parser.value("F").toInt();
    settings.m_packetFormat = parser.value("f").toInt();

    if (parser.isSet("a"))
    {
        // FM modulate the audio, scaled so its RMS is the deviation
        double sumSq = 0.0;

        for (const auto& s : samples) {
            sumSq += s.real() * s.real();
        }

        float scale = settings.m_fskDeviation / std::sqrt(sumSq / samples.size());
        double phase = 0.0;

        for (auto& s : samples)
        {
            phase += 2.0 * M_PI * scale * s.real() / rate;
            s = std::polar(1.0f, (float) std::fmod(phase, 2.0 * M_PI));
        }
    }

    MessageQueue queue;
    SSDVDemodSink sink;
    sink.applySettings(QStringList(), settings, true);
    sink.applyChannelSettings(rate, parser.value("c").toInt(), true);
    sink.setMessageQueueToChannel(&queue);
    SSDVDemodFrameParser frameParser;
    frameParser.setFormat(settings.m_packetFormat, settings.m_framing);

    // Scale to the sample range, with the peak at half of it
    float peak = 1e-9f;

    for (const auto& s : samples) {
        peak = std::max(peak, std::max(std::abs(s.real()), std::abs(s.imag())));
    }

    float scale = SDR_RX_SCALEF / 2.0f / peak;
    QMap<QString, SSDV::ImageDecoder *> decoders;
    int frames = 0;
    int packets = 0;
    const int blockSize = 4096;
    SampleVector block;

    for (size_t start = 0; start < samples.size(); start += blockSize)
    {
        size_t end = std::min(samples.size(), start + blockSize);
        block.resize(end - start);

        for (size_t i = start; i < end; i++) {
            block[i - start] = Sample(samples[i].real() * scale, samples[i].imag() * scale);
        }

        sink.feed(block.begin(), block.end());
        double t = end / (double) rate;
        Message *message;

        while ((message = queue.pop()))
        {
            if (SSDVDemodReport::MsgFrame::match(*message))
            {
                const SSDVDemodReport::MsgFrame& report = *(SSDVDemodReport::MsgFrame*) message;
                frames++;

                if (!parser.isSet("q"))
                {
                    printf("%7.2f s frame %3d bytes RS %d sync errors %d lock %d offset %+6.0f Hz EVM %.2f: %s\n", t,
                        (int) report.getData().size(), report.getRSErrors(), report.getASMErrors(), sink.isLocked(),
                        sink.getFrequencyOffset(), std::sqrt(sink.getEvm()), report.getData().left(24).toHex(' ').constData());
                }

                for (const auto& packet : frameParser.parse(report.getData()))
                {
                    SSDV::PacketInfo info;
                    QByteArray standardPacket;

                    if (!SSDV::parsePacket(packet.m_data, packet.m_variant, info, standardPacket)) {
                        continue;
                    }

                    packets++;
                    printf("%7.2f s packet %s image %d packet %d %dx%d RS %d%s\n", t, qPrintable(info.m_callsign), info.m_imageId,
                        info.m_packetId, info.m_width, info.m_height, info.m_rsErrors, info.m_eoi ? " EOI" : "");
                    QString key = QString("%1_%2").arg(info.m_callsign).arg(info.m_imageId);

                    if (!decoders.contains(key)) {
                        decoders.insert(key, new SSDV::ImageDecoder(info.m_callsign, info.m_imageId, standardPacket.size()));
                    }

                    decoders[key]->addPacket(standardPacket, info);
                }
            }

            delete message;
        }
    }

    printf("%.1f s: %d frames, %d bad, %d SSDV packets\n", samples.size() / (double) rate, frames, sink.getFramesBad(), packets);

    for (auto it = decoders.begin(); it != decoders.end(); ++it)
    {
        SSDV::ImageDecoder *decoder = it.value();
        printf("image %s #%d: %d packets received, %d missing%s\n", qPrintable(decoder->getCallsign()), decoder->getImageId(),
            decoder->getPacketsReceived(), decoder->getMissingPackets(), decoder->isComplete() ? ", complete" : "");

        if (parser.isSet("w"))
        {
            QFile file(QString("%1_%2.jpg").arg(parser.value("w")).arg(decoder->getImageId()));

            if (file.open(QIODevice::WriteOnly)) {
                file.write(decoder->jpeg());
            }
        }

        delete decoder;
    }

    return frames > 0 ? 0 : 1;
}
