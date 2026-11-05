<h1>SSDV demodulator plugin</h1>

<h2>Introduction</h2>

This plugin receives SSDV (Slow Scan Digital Video) images sent over a BPSK link with CCSDS framing and coding,
as used by the amateur satellite ASRTU-1 (AO-123) on 436.210 MHz: 9600 baud BPSK, CCSDS concatenated coding
(rate 1/2 K=7 convolutional code and Reed-Solomon (255,223)), the CCSDS randomizer and differential coding.

SSDV is a packetised form of JPEG: each packet can be decoded on its own, so a lost packet only blanks part
of the image, and packets of an image can be received in any order, over several passes.
The SSDV packet coding is [Philip Heron's SSDV library](https://codeberg.org/fsphil/ssdv).

Two packet formats are supported:

  - AO-123: each 223 byte frame holds a 5 byte TM header (virtual channel 1 for images) and one 218 byte SSDV
    packet in the headerless variant first used by DSLWP, as decoded by gr-satellites' `gr_satellites_ssdv --satellite DSLWP`.
  - Standard: standard 256 byte SSDV packets (with or without their own FEC), carried as a byte stream after the TM header
    of each frame, as sent by the SSDV modulator in that format.

The demodulator is:

  - a frequency locked loop on the squared signal, which pulls in offsets of up to half the symbol rate (AFC),
  - a root raised cosine matched filter and AGC,
  - Gardner symbol timing recovery,
  - a Costas loop,
  - soft decision Viterbi decoding of both symbol pairings, differential decoding, a search for the CCSDS sync marker
    (up to 4 bit errors), derandomization and Reed-Solomon decoding.

For satellites, use the Satellite Tracker's Doppler correction: the AFC follows the residual Doppler shift,
but not the whole shift at UHF.

<h2>Interface</h2>

<h3>Settings</h3>

  - Df: frequency of the signal relative to the device center frequency.
  - Baud: symbol rate.
  - RRC: root raised cosine roll-off, in %.
  - PLL: Costas loop bandwidth in thousandths of the symbol rate.
  - AFC: frequency locked loop ahead of the Costas loop.
  - Format: AO-123 or Standard (see above). Choosing AO-123 also sets the coding AO-123 uses.
  - Frame: CCSDS frame size in bytes without the RS parity (223 for AO-123).
  - Conv, Diff, Scr, RS, Dual: convolutional code, differential coding, randomizer, Reed-Solomon code,
    and Reed-Solomon in dual rather than conventional basis.
  - Status: carrier lock, frequency offset tracked by the AFC and Costas loop, error vector magnitude,
    frames received / frames that failed RS decoding, and SSDV packets received.
  - Save: save each image as JPEG when it is complete, to the given folder (Pictures/SSDV if empty).
    Incomplete images are saved (with `_partial` in the name) when replaced by others, if at least a quarter was received.
  - Frames: append each frame received to a KISS file, as gr-satellites' `--kiss_out` does, for analysis with other tools.

<h3>Image</h3>

The image being received, with its callsign (DSLWP for AO-123, whose packets carry none), image ID and packets
received and missing. Missing parts are grey. The save button saves the most recent image, the bin button forgets
the images being received.

<h3>Packets</h3>

The SSDV packets received: packet ID, image size, first MCU in the packet, last packet of the image (EOI),
bytes corrected by the packet's own RS code (standard packets with FEC) and whether it is a repeat.

<h3>Constellation</h3>

The symbols, as an XY plot: two tight clusters on the I axis when the signal is good and locked.

<h2>API</h2>

Full details of the API can be found in the Swagger documentation. Settings are under `SSDVDemodSettings`,
and the report (`SSDVDemodReport`) gives the lock state, frequency offset, frame and packet counts and the
progress of the most recent image.
