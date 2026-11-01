<h1>VARA demodulator plugin</h1>

<h2>Introduction</h2>

This plugin runs a VARA HF/FM compatible modem, [xara](https://github.com/x0rro/xara), on a receive channel. Winlink
clients (E.g. Pat or Winlink Express) and other VARA host programs connect to its VARA TCP command and data ports, just
as they would to VARA on a sound card.

The demodulator owns the modem and feeds it with the received signal, as a radio's data port would: flat FM
discriminator output in the FM modes, USB audio in the HF modes. To transmit, add a [VARA modulator](../../channeltx/modvara/readme.md)
with the same modem port to a Tx device set (or the Tx stream of the same MIMO device set, E.g. PlutoSDR MIMO). Without
one the modem receives only: it is told that PTT can't be keyed, so it never believes it transmitted.

The modem's ports are open as long as the channel exists, even when the device is stopped, but it only receives (and
so only runs its timers) while samples flow.

The plugin is only built when SDRangel is configured with `-DENABLE_XARA=ON` and a xara source tree with its C API
(`crates/xara-ffi`) is given with `-DXARA_DIR=...` (default `../xara`). The modem library is built with cargo.

<h2>Interface</h2>

<h3>Settings</h3>

* **Df**: frequency shift of the channel from the device center frequency. In the HF modes the channel frequency is the
  suppressed carrier (dial) frequency.
* **Channel power** in dB, and the **audio mute** button: left click to mute or unmute the monitor audio, right click to
  select the audio output device.
* **Mode**: VARA FM narrow and wide, and VARA HF 500, 2300 and 2750. Changing it restarts the modem, which drops any
  connection.
* **Call**: your call sign, with an optional SSID. The modem doesn't start without one.
* **Link**: keep the frequency offset of linked VARA modulators in sync with this channel, in any device set, as for the
  SSB, NFM, AM and packet channels. The Address Book can also switch to VARA channels when this is on.
* **Port**: the VARA command TCP port. The data port is the next one (8300 and 8301 by default, as VARA).
* **KISS**: open a KISS TCP port too (8100 by default).
* **Addr**: listen address of the TCP ports. 127.0.0.1 only accepts clients from this machine; use 0.0.0.0 to accept
  clients from others.
* **Drive**: transmit drive of the modem, -30 to 0 dB.
* **Rx lag**: delay from RF to the modem's input, used by the modem for its turnaround timing.
* **Dev**: FM peak deviation that gives full scale modem input, in the FM modes.
* **AGC** and gain: in the HF modes the modem's input is kept at about -12 dBFS RMS by an AGC with a fast attack and a
  slow decay, or set by a fixed gain when the AGC is off.
* **Vol**: monitor audio volume, and the **In** meter: level of the modem's input in dB full scale.

<h3>Status</h3>

* **State** and **Peer**: session state (E.g. Listen, Connected) and the remote station while linked.
* **BW**: link bandwidth and speed level.
* **AFC**: frequency offset of the received signal.
* **Ports**: the TCP ports the modem has open.
* **Flags**: TX while transmitting, BUSY when the channel is busy, TUNE while the tune tone is on, and whether a VARA
  modulator transmits this modem.

Errors that keep the modem from running (no call sign, a port already in use) are shown below.

<h3>Log</h3>

What the modem shows on its console, time stamped: the host's commands (`>`) and the modem's replies (`<`), PTT and
busy changes, and the frames it sends and receives on its own link. A VARA modem only follows its own link, so this is
not a monitor of other stations' traffic.

* **Record** button: start/stop appending the log lines to a file, and the **save** button to choose it
  (`vara_log.txt` in the working directory by default). Lines are written as they come, with the date and time.
* **Bin** button: clear the lines shown (the file is kept).

xara doesn't say which modem the lines of its TCP host (shared by all modems) come from, so with more than one VARA
demodulator these appear in all of them. The lines of each modem only appear in its own.

<h2>API</h2>

Full details of the API can be found in the Swagger documentation. The report includes the modem's state:

    curl -X GET "http://127.0.0.1:8091/sdrangel/deviceset/0/channel/0/report"
