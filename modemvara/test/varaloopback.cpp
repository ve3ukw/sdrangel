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

// Two VARA stations back to back, each a VARAEngine with the VARA Mod's source and the VARA Demod's sink,
// cross-wired at a 96 kHz channel rate with no radio: checks the FM and USB modulation, the scaling, the
// transmit FIFO and the PTT callback, by making a VARA connection between them. Station A also keys a
// UDPPTT, whose datagrams a local socket checks, as pttseq would get them.
//
//   varaloopback [-r] [-p] [mode...]    modes as VARAEngine::Mode, default: FM narrow and HF 2300
//                                       -r: both stations in registered mode
//                                       -p: both hosts ask for a P2P session

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <random>

#include <QCoreApplication>
#include <QDir>
#include <QThread>
#include <QStringList>

#include "dsp/dsptypes.h"
#include "util/udpptt.h"
#include "varaengine.h"
#include "varatxlink.h"
#include "varademodsink.h"
#include "varamodsource.h"

namespace {

const int channelRate = 96000;
const int chunk = channelRate / 100; // 10 ms
bool registered = false;
bool p2p = false;

struct Station
{
    VARAEngine engine;
    std::shared_ptr<VARATxLink> link;
    VARAModSource mod;
    VARADemodSink demod;
    int keyEdges = 0;
    int fd = -1;
    QByteArray seen;
    QStringList log; //!< xara's info lines for this modem (and the shared host's)

    Station(int mode, const char *call, quint16 port)
    {
        VARAEngine::Config config;
        config.m_mode = mode;
        config.m_call = call;
        config.m_cmdPort = port;
        config.m_registered = registered;
        engine.setConfig(config);

        engine.setLogCallback([this](const QDateTime&, const QString& line) { log.append(line); });
        link = VARATxLink::create(port);
        link->setTiming(60 * 48, 40 * 48, 0); // as the Mod's defaults
        link->setPttCallback([this](bool) { keyEdges++; });
        mod.setTxLink(link);
        mod.applyChannelSettings(channelRate, 0);
        demod.setEngine(&engine);
        demod.applyChannelSettings(channelRate, 0);

        VARADemodSettings settings;
        settings.m_mode = mode;
        demod.applySettings(QStringList("mode"), settings);
    }

    bool connectHost()
    {
        VARAEngine::Status status = engine.getStatus();

        if (!status.m_running)
        {
            fprintf(stderr, "modem not running: %s\n", qPrintable(status.m_error));
            return false;
        }

        fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = htons(status.m_cmdPort);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::connect(fd, (sockaddr *) &a, sizeof(a)) != 0)
        {
            perror("connect");
            return false;
        }

        fcntl(fd, F_SETFL, O_NONBLOCK);
        return true;
    }

    void send(const char *command) {
        (void) !write(fd, command, strlen(command));
    }

    void poll()
    {
        char buf[1024];
        ssize_t n;

        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            seen.append(buf, n);
        }
    }

    ~Station()
    {
        if (fd >= 0) {
            close(fd);
        }
    }
};

// Tx samples to Rx samples, which may have a different scale, with a little noise
void channel(const SampleVector& tx, SampleVector& rx, std::mt19937& rng)
{
    std::normal_distribution<float> noise(0.0f, 0.001f * SDR_RX_SCALEF);
    const float scale = SDR_RX_SCALEF / SDR_TX_SCALEF;

    for (size_t i = 0; i < tx.size(); i++)
    {
        rx[i].m_real = (FixReal) (tx[i].m_real * scale + noise(rng));
        rx[i].m_imag = (FixReal) (tx[i].m_imag * scale + noise(rng));
    }
}

// A UDP socket standing in for pttseq, collecting the commands it gets
struct Sequencer
{
    int fd;
    quint16 port;
    QStringList commands;

    Sequencer()
    {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(fd, (sockaddr *) &a, sizeof(a));
        socklen_t len = sizeof(a);
        getsockname(fd, (sockaddr *) &a, &len);
        port = ntohs(a.sin_port);
        fcntl(fd, F_SETFL, O_NONBLOCK);
    }

    void poll()
    {
        char buf[64];
        ssize_t n;

        while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
            commands.append(QString::fromUtf8(buf, n));
        }
    }

    ~Sequencer() { close(fd); }
};

bool run(int mode)
{
    printf("%s: ", qPrintable(VARAEngine::getModeName(mode)));
    fflush(stdout);
    Station a(mode, "N0AAA", 18300);
    Station b(mode, "N0BBB", 18310);
    Sequencer sequencer;
    auto ptt = std::make_shared<UDPPTT>();
    ptt->setDestination("127.0.0.1", sequencer.port);
    ptt->setTailMs(100);
    a.link->setPttCallback([&a, ptt](bool keyed) { a.keyEdges++; ptt->setPtt(keyed); });

    if (!a.connectHost() || !b.connectHost()) {
        return false;
    }

    std::mt19937 rng(1);
    SampleVector txA(chunk), txB(chunk), rxA(chunk), rxB(chunk);
    bool sent = false;

    for (int i = 0; i < 100 * 120; i++) // 2 minutes of samples, run as fast as they can be
    {
        a.mod.pull(txA.begin(), chunk);
        b.mod.pull(txB.begin(), chunk);
        channel(txA, rxB, rng);
        channel(txB, rxA, rng);
        a.demod.feed(rxA.begin(), rxA.end());
        b.demod.feed(rxB.begin(), rxB.end());

        if (!sent && (i == 50)) // give the host threads a moment
        {
            if (p2p)
            {
                a.send("P2P SESSION\r");
                b.send("P2P SESSION\r");
            }

            a.send("CONNECT N0AAA N0BBB\r");
            sent = true;
        }

        a.poll();
        b.poll();
        sequencer.poll();

        if (a.seen.contains("CONNECTED ") && b.seen.contains("CONNECTED "))
        {
            VARAEngine::Status status = a.engine.getStatus();
            printf("connected after %.1f s, peer %s, %s level %u, key edges %d/%d, underflows %u/%u\n",
                i / 100.0, qPrintable(status.m_peer), qPrintable(status.m_bandwidth), status.m_level,
                a.keyEdges, b.keyEdges, a.link->getUnderflows(), b.link->getUnderflows());

            // Release PTT, then wait (in real time) for the tail before "PTT 0" is sent
            ptt->setPtt(false);
            QThread::msleep(300);
            sequencer.poll();
            printf("  sequencer got: %s\n", qPrintable(sequencer.commands.join(", ")));
            bool pttOk = sequencer.commands.contains("PTT 1") && (sequencer.commands.last() == "PTT 0");
            printf("  A logged %d lines, B %d. A's:\n    %s\n", (int) a.log.size(), (int) b.log.size(),
                qPrintable(a.log.join("\n    ")));
            // Only A was sent the CONNECT command, so only A's log echoes it
            bool logOk = !a.log.filter("> CONNECT", Qt::CaseInsensitive).isEmpty()
                && b.log.filter("> CONNECT", Qt::CaseInsensitive).isEmpty();
            // B's host is told whether the caller is registered
            bool registeredOk = b.seen.contains(registered ? "LINK REGISTERED" : "LINK UNREGISTERED");
            printf("  B's host was told: %s\n", registeredOk ? (registered ? "LINK REGISTERED" : "LINK UNREGISTERED") : "neither");
            // Only HF keeps the session type
            bool hf = (mode != VARAEngine::FMNarrow) && (mode != VARAEngine::FMWide);
            bool p2pOk = (status.m_p2p == (p2p && hf)) && (b.engine.getStatus().m_p2p == (p2p && hf));
            printf("  session: %s\n", hf ? (status.m_p2p ? "P2P" : "Winlink") : "-");

            return a.seen.contains("CONNECTED N0AAA N0BBB") && (status.m_peer == "N0BBB")
                && (a.keyEdges > 0) && (b.keyEdges > 0) && pttOk && logOk && registeredOk && p2pOk;
        }
    }

    printf("no connection. A saw %s, B saw %s\n", a.seen.trimmed().constData(), b.seen.trimmed().constData());
    return false;
}

}

int main(int argc, char *argv[])
{
    // MainCore keeps settings, so keep them away from the real ones
    QString config = QDir::temp().filePath("varaloopback-config");
    qputenv("XDG_CONFIG_HOME", config.toUtf8());
    QCoreApplication app(argc, argv);
    QList<int> modes;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-r") == 0) {
            registered = true;
        } else if (strcmp(argv[i], "-p") == 0) {
            p2p = true;
        } else {
            modes.append(atoi(argv[i]));
        }
    }

    if (modes.isEmpty()) {
        modes = {VARAEngine::FMNarrow, VARAEngine::HF2300};
    }

    bool ok = true;

    for (int mode : modes) {
        ok = run(mode) && ok;
    }

    printf("%s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
