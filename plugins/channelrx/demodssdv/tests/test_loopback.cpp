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

// Loopback of the SSDV Mod source into the SSDV Demod sink, with noise, frequency offset and sample rate error:
//   ssdv_loopback [-f format] [-e EbN0dB] [-o offsetHz] [-p ppm] [-b baud] [-r channelRate] [-i image] [-w out.jpg]
// format is 0 for AO-123, 1 for standard packets. Exits with 0 if the image is received complete.

#include <cstdio>
#include <random>

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QImage>
#include <QPainter>
#include <QFile>

#include "util/messagequeue.h"
#include "util/ssdvcodec.h"

#include "ssdvdemodsink.h"
#include "ssdvdemodreport.h"
#include "ssdvdemodframeparser.h"
#include "../../../channeltx/modssdv/ssdvmodsource.h"
#include "../../../channeltx/modssdv/ssdvmodframer.h"
#include "../../../channeltx/modssdv/ssdvmodreport.h"

static QImage testImage()
{
    QImage image(320, 256, QImage::Format_RGB888);

    for (int y = 0; y < image.height(); y++) {
        for (int x = 0; x < image.width(); x++) {
            image.setPixel(x, y, qRgb(x * 255 / 320, y * 255 / 256, ((x / 32) + (y / 32)) % 2 ? 200 : 40));
        }
    }

    QPainter painter(&image);
    painter.setPen(Qt::white);
    painter.drawEllipse(60, 40, 200, 176);
    painter.end();

    return image;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOption({"f", "Packet format", "format", "0"});
    parser.addOption({"e", "Eb/N0 in dB", "ebn0", "6"});
    parser.addOption({"o", "Frequency offset in Hz", "offset", "1000"});
    parser.addOption({"p", "Sample rate error in ppm", "ppm", "50"});
    parser.addOption({"b", "Baud rate", "baud", "9600"});
    parser.addOption({"r", "Channel sample rate", "rate", "96000"});
    parser.addOption({"i", "Image file", "image"});
    parser.addOption({"w", "Write received JPEG", "file"});
    parser.process(app);

    int format = parser.value("f").toInt();
    float ebn0 = parser.value("e").toFloat();
    float offset = parser.value("o").toFloat();
    double ppm = parser.value("p").toDouble();
    int baud = parser.value("b").toInt();
    int rate = parser.value("r").toInt();
    QImage image = parser.isSet("i") ? QImage(parser.value("i")) : testImage();

    // Transmitter
    SSDVModSettings modSettings;
    modSettings.m_baudRate = baud;
    modSettings.m_packetFormat = format;
    modSettings.m_callsign = "VE3UKW";
    modSettings.m_imageId = 42;
    modSettings.m_preambleMs = 500;
    image = SSDV::Encoder::fit(image, modSettings.m_width, modSettings.m_height, false);
    SSDV::Encoder encoder(format == SSDVModSettings::AO123 ? SSDV::DSLWP : SSDV::Standard, modSettings.m_callsign, modSettings.m_imageId);
    QString error;
    QList<QByteArray> packets = encoder.encode(image, error);

    if (packets.isEmpty())
    {
        fprintf(stderr, "encode failed: %s\n", qPrintable(error));
        return 2;
    }

    std::vector<uint8_t> symbols;
    std::vector<std::pair<int, int>> progress;
    SSDVModFramer framer;
    framer.build(modSettings, packets, symbols, progress);

    MessageQueue modQueue;
    SSDVModSource source;
    source.applySettings(QStringList(), modSettings, true);
    source.applyChannelSettings(rate, 0, true);
    source.setMessageQueueToChannel(&modQueue);
    source.transmit(symbols, progress);

    // Receiver
    SSDVDemodSettings demodSettings;
    demodSettings.m_baudRate = baud;
    demodSettings.m_packetFormat = format;
    MessageQueue demodQueue;
    SSDVDemodSink sink;
    sink.applySettings(QStringList(), demodSettings, true);
    // The receiver runs at a slightly different rate, and is tuned off the signal
    int rxRate = std::round(rate * (1.0 + ppm * 1e-6));
    sink.applyChannelSettings(rxRate, (int) -offset, true);
    sink.setMessageQueueToChannel(&demodQueue);
    SSDVDemodFrameParser frameParser;
    frameParser.setFormat(format, SSDVDemodSettings::CCSDS);

    // Noise for the Eb/N0, from the signal power while transmitting
    std::mt19937 rng(1);
    std::normal_distribution<float> normal(0.0f, 1.0f);
    const int blockSize = 4096;
    SampleVector txBlock(blockSize);
    SampleVector rxBlock(blockSize);
    double powerSum = 0.0;
    long powerCount = 0;
    float sigma = 0.0f;
    bool done = false;
    int frames = 0;
    SSDV::ImageDecoder *decoder = nullptr;
    long silentBlocks = 0;

    while (!done || (silentBlocks < 20))
    {
        source.pull(txBlock.begin(), blockSize);

        for (int i = 0; i < blockSize; i++)
        {
            double p = (double) txBlock[i].real() * txBlock[i].real() + (double) txBlock[i].imag() * txBlock[i].imag();

            if (p > 0.0)
            {
                powerSum += p;
                powerCount++;
            }
        }

        if ((sigma == 0.0f) && (powerCount > 100000))
        {
            // Es = power * samples per symbol, Eb = Es / rate (1/2 with the convolutional code)
            double es = powerSum / powerCount * rate / baud;
            double eb = es * (modSettings.m_convolutional ? 2.0 : 1.0);
            double n0 = eb / std::pow(10.0, ebn0 / 10.0);
            sigma = std::sqrt(n0 / 2.0);
        }

        for (int i = 0; i < blockSize; i++)
        {
            float re = txBlock[i].real() + sigma * normal(rng);
            float im = txBlock[i].imag() + sigma * normal(rng);
            rxBlock[i] = Sample(std::max(-32767.0f, std::min(32767.0f, re)), std::max(-32767.0f, std::min(32767.0f, im)));
        }

        sink.feed(rxBlock.begin(), rxBlock.end());

        Message *message;

        while ((message = modQueue.pop()))
        {
            if (SSDVModReport::MsgProgress::match(*message) && ((SSDVModReport::MsgProgress*) message)->getDone()) {
                done = true;
            }

            delete message;
        }

        while ((message = demodQueue.pop()))
        {
            if (SSDVDemodReport::MsgFrame::match(*message))
            {
                frames++;

                for (const auto& packet : frameParser.parse(((SSDVDemodReport::MsgFrame*) message)->getData()))
                {
                    SSDV::PacketInfo info;
                    QByteArray standardPacket;

                    if (SSDV::parsePacket(packet.m_data, packet.m_variant, info, standardPacket))
                    {
                        if (!decoder) {
                            decoder = new SSDV::ImageDecoder(info.m_callsign, info.m_imageId, standardPacket.size());
                        }

                        decoder->addPacket(standardPacket, info);
                    }
                }
            }

            delete message;
        }

        if (done) {
            silentBlocks++;
        }
    }

    bool complete = decoder && decoder->isComplete();
    printf("format %d baud %d rate %d Eb/N0 %.1f dB offset %.0f Hz %+.0f ppm: %d packets sent in %.1f s, frames %d bad %d, "
        "image %s #%d received %d missing %d %s, lock %d, offset %.0f Hz\n",
        format, baud, rate, ebn0, offset, ppm, (int) packets.size(), symbols.size() / (double) baud, frames, sink.getFramesBad(),
        decoder ? qPrintable(decoder->getCallsign()) : "-", decoder ? decoder->getImageId() : -1,
        decoder ? decoder->getPacketsReceived() : 0, decoder ? decoder->getMissingPackets() : 0,
        complete ? "complete" : "INCOMPLETE", sink.isLocked(), sink.getFrequencyOffset());

    if (decoder && parser.isSet("w"))
    {
        QFile file(parser.value("w"));

        if (file.open(QIODevice::WriteOnly)) {
            file.write(decoder->jpeg());
        }
    }

    delete decoder;
    return complete ? 0 : 1;
}
