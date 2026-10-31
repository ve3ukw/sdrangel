<h1>VARA modulator plugin</h1>

<h2>Introduction</h2>

This plugin transmits the VARA modem of a [VARA demodulator](../../channelrx/demodvara/readme.md): the one whose modem
(command) port it is set to. The modem runs on the receive side, which drives its clock from the received samples, and
its transmit audio is modulated here, as a radio with a data port would: flat FM in the FM modes, USB in the HF modes.

As the modem keys, the modulator can key a PTT sequencer over UDP, such as pttseq on a PlutoSky. It sends `PTT 1`
when the modem keys, repeats it every 500 ms as a heartbeat while keyed, and sends `PTT 0` after the tail when the modem
releases. pttseq releases by itself when the heartbeat stops for 1.5 s. The device keeps streaming while unkeyed (the
sequencer gates the transmitter), so the transmit path delay stays constant.

The modem can only key while this channel is running and not muted. Muting the channel, stopping the device or
changing the modem port detaches it from the modem, and releases PTT.

<h2>Timing</h2>

The transmit audio goes through a buffer that holds the PTT lead: PTT is keyed when the first sample enters it, and
that sample reaches the modulator **Lead** ms later. Between transmissions the buffer is kept between the lead and the
lead plus 40 ms, which takes up timing jitter and any Rx/Tx clock difference. The modem is told the whole delay, from
its output to RF (the lead, 20 ms of that window, and the **Tx path** delay), and holds PTT for that long after its
last sample, plus the **Tail**.

Set the lead to the time the sequencer takes to key (relay, PA, TX LO) less the Tx path delay, so the first sample
reaches RF just as the transmitter is ready.

<h2>Interface</h2>

<h3>Settings</h3>

* **Df**: frequency shift of the channel from the device center frequency. In the HF modes the channel frequency is the
  suppressed carrier (dial) frequency.
* **Channel power** in dB, and the **mute** button, which also detaches the channel from the modem.
* **Lvl**: level of the modem's output, before the gain.
* **Modem port**: the command port of the VARA demodulator whose modem is transmitted (8300 by default, as the
  demodulator, so they pair without setup).
* **Dev**: FM peak deviation for full scale modem output, in the FM modes.
* **Gain**: output gain.
* **Link**: keep the frequency offset of linked VARA demodulators in sync with this channel.
* **PTT**, host and port: key a UDP PTT sequencer (192.168.2.1:7373 by default, pttseq on a PlutoSky), and **?** to ask
  it for its status.
* **Lead**, **Tail** and **Tx path**: see Timing above.

<h3>Status</h3>

* **Modem**: the mode of the modem transmitted, or why there is none.
* **Tx**: keyed or idle, and the transmit audio buffered.
* **Underruns**: times the transmit audio ran out while keyed.
* **PTT**: the last reply (or error) from the PTT sequencer.

<h2>API</h2>

Full details of the API can be found in the Swagger documentation.
