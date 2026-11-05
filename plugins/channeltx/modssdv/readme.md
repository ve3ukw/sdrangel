<h1>SSDV modulator plugin</h1>

<h2>Introduction</h2>

This plugin sends images as SSDV (Slow Scan Digital Video) packets over BPSK with CCSDS framing and coding,
in the format of the amateur satellite ASRTU-1 (AO-123), or as standard SSDV packets. It is the counterpart of the
SSDV demodulator.

Each image is scaled to the chosen size (a multiple of 16), JPEG encoded and cut into SSDV packets with
[Philip Heron's SSDV library](https://codeberg.org/fsphil/ssdv). The packets are put in 223 byte CCSDS TM frames
(5 byte header with spacecraft ID, virtual channel and counters), then Reed-Solomon (255,223) encoded, randomized,
differentially coded, convolutionally coded (rate 1/2, K=7) and sent as root raised cosine shaped BPSK.

  - AO-123 format: one 218 byte DSLWP-style SSDV packet per frame (no callsign).
  - Standard format: standard 256 byte SSDV packets with FEC, carried as a byte stream after the TM header.

Idle frames (virtual channel 7) are sent before each image so the receiver can lock.

PTT is not handled by the plugin: the channel only produces samples while sending an image.

<h2>Interface</h2>

<h3>Settings</h3>

  - Df: frequency offset of the signal from the device center frequency. The button mutes the channel.
  - Gain: output gain in dB. Peaks of the shaped signal are clipped at full scale.
  - Baud: symbol rate. RRC: root raised cosine roll-off in %.
  - Call: callsign in standard SSDV packets (AO-123 packets carry none).
  - SCID, VC: TM frame spacecraft ID and virtual channel of image frames.
  - Format, Frame, Conv, Diff, Scr, RS, Dual: as in the SSDV demodulator. Choosing AO-123 also sets the coding it uses.

<h3>Image</h3>

  - Image: an image file, or a folder whose images (in name order) are sent in turn.
  - ID: SSDV image ID of the next image. It is incremented after each image, so receivers can tell images apart.
  - Q: SSDV quality level, 0 (smallest) to 7 (best). 4 is the SSDV default.
  - Size: image size; Fit scales the whole image in and pads it with black, rather than cropping it to fill.
  - Preamble: duration of the idle frames before the image.
  - Repeat, Delay: send images continuously, with a delay between them.
  - TX sends the image (or the next one of the folder), Stop stops after the filter has rung out.

At 9600 baud, an AO-123 frame takes 0.43 s; a 320x256 image is typically 15 to 40 packets.

<h2>API</h2>

Full details of the API can be found in the Swagger documentation. Settings are under `SSDVModSettings`, and the
report (`SSDVModReport`) gives the progress of the image being sent. The `SSDVModActions` action `tx` sends the image
(or the next of the folder), as the TX button does, and `stop` stops sending.
