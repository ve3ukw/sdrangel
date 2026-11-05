<h1>SSDV demodulator plugin</h1>

<h2>Introduction</h2>

This plugin receives SSDV (Slow Scan Digital Video) images from amateur satellites. Three links are supported:

| Satellite | Downlink | Modulation | Framing |
|---|---|---|---|
| ASRTU-1 (AO-123) | 436.210 MHz | 9600 baud BPSK | CCSDS concatenated coding |
| Luca (RS90S) | 437.180 MHz | 9600 or 2400 baud GMSK | Unified SPUTNIX Protocol (USP) |
| HADES-SA (SO-127) | 436.875 MHz | 800 baud 2-FSK, tones 1600 Hz apart | AMSAT-EA GENESIS |

The Link setting sets everything for each of them.

SSDV is a packetised form of JPEG: each packet can be decoded on its own, so a lost packet only blanks part
of the image, and packets of an image can be received in any order, over several passes.
The SSDV packet coding is [Philip Heron's SSDV library](https://codeberg.org/fsphil/ssdv).

<h3>CCSDS framing (AO-123)</h3>

CCSDS concatenated coding: rate 1/2 K=7 convolutional code, Reed-Solomon (255,223), the CCSDS randomizer and
differential coding, each of which can be turned off. The frames can hold SSDV packets in two formats:

  - AO-123: each 223 byte frame holds a 5 byte TM header (virtual channel 1 for images) and one 218 byte SSDV
    packet in the headerless variant first used by DSLWP, as decoded by gr-satellites' `gr_satellites_ssdv --satellite DSLWP`.
  - Standard: standard 256 byte SSDV packets (with or without their own FEC), carried as a byte stream after the TM header
    of each frame, as sent by the SSDV modulator in that format.

<h3>USP framing (Luca)</h3>

The Unified SPUTNIX Protocol, as decoded by gr-satellites: a 64 bit sync word, a PLS code that gives the frame
length (48 or 223 bytes), the CCSDS convolutional code, randomizer and Reed-Solomon code (in dual basis), and an
AX.25 frame inside. The demodulator looks for SSDV packets in the AX.25 information fields, in the format chosen with
Format (Standard: 256 byte packets as a byte stream, which may span frames).

Luca's telemetry decodes (from SatNOGS recordings at 9600 baud), but its SSDV packets have not been seen yet,
so how they are carried is a guess. The received frames can be saved to a KISS file (Frames) to find out.

<h3>GENESIS framing (HADES-SA)</h3>

AMSAT-EA's framing for HADES-SA and HADES-L, after AMSAT-EA's documents and decoder and the
[HADESx decoder](https://github.com/N6RFM/HADESx-Decoder): 0xAA training, the 0xBF35 sync word, a size byte, a type and
source address byte, the data (scrambled with x^17+x^12+1) and a CRC-16. SSDV packets (type 10) are standard 256 byte
SSDV packets with FEC, sent unscrambled without their first two bytes; the satellite's frame header takes the
place of the callsign, which shows as `2-R3-R`.

Telemetry frames with a good CRC are counted and saved (from the type byte on, descrambled, without the CRC, which
AMSAT-EA's decoder takes), but not decoded. Voice and PN9 test frames are ignored.

<h3>Demodulators</h3>

The BPSK demodulator is:

  - a frequency locked loop on the squared signal, which pulls in offsets of up to half the symbol rate (AFC),
  - a root raised cosine matched filter and AGC,
  - Gardner symbol timing recovery,
  - a Costas loop.

The FSK demodulator, which also receives GMSK, is:

  - an FM discriminator after a filter passing both tones,
  - removal of the mean frequency (AFC), which follows offsets of up to twice the deviation,
  - integration over a symbol and Gardner symbol timing recovery.

The soft decisions go to the Viterbi decoder for the convolutional codes.

For satellites, use the Satellite Tracker's Doppler correction: the AFC follows the residual Doppler shift,
but not the whole shift at UHF.

<h2>Interface</h2>

<h3>Settings</h3>

  - Df: frequency of the signal relative to the device center frequency.
  - Link: sets the modulation, baud rate, deviation, framing, format and coding for AO-123, Luca (9600 or 2400 baud)
    or HADES-SA. Custom when the settings are not those of a listed link.
  - Mod: BPSK, or FSK (which also receives GMSK).
  - Framing: CCSDS, USP or GENESIS (see above).
  - Baud: symbol rate.
  - Dev: FSK, frequency of each tone from the centre (half the tone spacing): 800 Hz for HADES-SA, a quarter of the baud
    rate for GMSK.
  - RRC: BPSK, root raised cosine roll-off, in %.
  - PLL: BPSK, Costas loop bandwidth in thousandths of the symbol rate.
  - AFC: frequency correction ahead of the demodulator.
  - Format: CCSDS and USP, AO-123 or Standard packets (see above).
  - Frame: CCSDS frame size in bytes without the RS parity (223 for AO-123).
  - Conv, Diff, Scr, RS, Dual: CCSDS convolutional code, differential coding, randomizer, Reed-Solomon code,
    and Reed-Solomon in dual rather than conventional basis.
  - Status: lock, frequency offset tracked by the AFC (and Costas loop), error vector magnitude (FSK: error of the
    symbol magnitude), frames received / frames that failed RS decoding or the CRC, and SSDV packets received.
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
