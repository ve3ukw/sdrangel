<h1>SSTV Demodulator Plugin</h1>

<h2>Introduction</h2>

This plugin receives slow-scan television (SSTV) images sent on FM, as transmitted by the ISS (145.800 MHz) and amateur radio satellites on VHF and UHF.

The FM signal is demodulated and the image tones (1500 Hz black to 2300 Hz white, 1200 Hz sync) are measured with an instantaneous frequency estimator that is weighted by signal amplitude, so images can be received at low signal to noise ratios.

The following modes are supported:

| Mode       | VIS | Size    | Duration |
|------------|-----|---------|----------|
| Robot 36   | 8   | 320x240 | 36 s     |
| Robot 72   | 12  | 320x240 | 72 s     |
| PD 50      | 93  | 320x256 | 50 s     |
| PD 90      | 99  | 320x256 | 90 s     |
| PD 120     | 95  | 640x496 | 126 s    |
| PD 160     | 98  | 512x400 | 161 s    |
| PD 180     | 96  | 640x496 | 187 s    |
| PD 240     | 97  | 640x496 | 248 s    |
| PD 290     | 94  | 800x616 | 289 s    |
| Martin 1   | 44  | 320x256 | 114 s    |
| Martin 2   | 40  | 320x256 | 58 s     |
| Scottie 1  | 60  | 320x256 | 110 s    |
| Scottie 2  | 56  | 320x256 | 71 s     |
| Scottie DX | 76  | 320x256 | 269 s    |

The ISS usually transmits PD 120 or PD 180, and sometimes Robot 36.

The VIS code sent before each image selects the mode and gives the start of the image. Each line's sync pulse is then searched for near its expected position,
and the line rate is measured from the sync pulses found, so images are straightened (slant correction) when the transmitter's clock differs from the nominal rate,
and lines whose sync pulse is lost in noise are still placed correctly.
Images are displayed line by line as they are received, and can be saved automatically when complete.

The demodulated audio can also be listened to.

The Satellite Tracker tunes this demodulator to SSTV transmitters, with Doppler correction, from the satellite table's context menu.

<h2>Interface</h2>

The top and bottom bars of the channel window are described [here](../../../sdrgui/channel/readme.md).

The window has two sections: the settings and the image.

<h3>1: Frequency shift from center frequency of reception</h3>

Use the wheels to adjust the frequency shift in Hz from the center frequency of reception. Left click on a digit sets the cursor position at this digit. Right click on a digit sets all digits on the right to zero. This effectively floors value at the digit position. Wheels are moved with the mousewheel while pointing at the wheel or by selecting the wheel with the left mouse click and using the keyboard arrows. Pressing shift simultaneously moves digit by 5 and pressing control moves it by 2.

<h3>2: Channel power</h3>

Average total power in dB relative to a +/- 1.0 amplitude signal received in the pass band.

<h3>3: Audio mute and audio output select</h3>

Left click on this button to toggle audio mute for this channel. The decoder keeps running when the audio is muted.

If you right click on it it will open a dialog to select the audio output device. See [audio management documentation](../../../sdrgui/audio.md) for details.

<h3>4: Level meter in dB</h3>

  - top bar (green): average value
  - bottom bar (blue green): instantaneous peak value
  - tip vertical bar (bright green): peak hold value

<h3>5: RF bandwidth</h3>

Bandwidth of the channel filter, before the FM demodulator. 15 kHz suits the 5 kHz deviation used by the ISS and most satellites. Reduce it for signals with less deviation to improve the signal to noise ratio,
but leave room for Doppler shift if the frequency isn't being tracked.

<h3>6: FM deviation</h3>

Peak FM deviation. This only sets the level of the audio output: the decoder measures tone frequencies, so it doesn't depend on the deviation.

<h3>7: Volume</h3>

Audio volume, in percent.

<h3>8: Mode</h3>

  - Auto: the mode is selected from the VIS code at the start of each image. Images with VIS codes of modes that aren't supported are ignored.
  - A mode: images are decoded in this mode whatever their VIS code. When the VIS code was missed (E.g. because reception started part way through an image),
  an image is also started when two sync pulses of the mode are received a whole number of lines apart. The image then starts at the first line received.

<h3>9: Status</h3>

Shows the mode and VIS code of the image (or "sync" when it was started from sync pulses), the number of lines received,
and the measured line rate error of the transmitter in parts per million (ppm), which the image is corrected for.

An image ends when all of its lines have been received, when the VIS code of a new image is received,
or when no sync pulses have been found for 5 seconds and there is no longer any SSTV signal.

<h3>10: Auto save</h3>

When on, images are saved as PNG files in the folder given on the right when they end.
Images that end early are saved with "_partial" in their name if at least a quarter of the lines were received. File names include the date and time and the mode, E.g. `SSTV_20261020_143012_PD120.png`.

<h3>11: Auto save folder</h3>

Folder for automatically saved images. When empty, images are saved in the SSTV folder of your Pictures folder. Click the button to the right to select a folder.

<h3>12: Save image</h3>

Saves the image being displayed to a file. While an image is being received, the lines received so far are saved.

<h3>13: Clear</h3>

Clears the image. An image being received is abandoned, and the decoder waits for the next VIS code.

<h3>14: Image</h3>

The image is drawn as it is received, scaled to fit the window.

<h2>Receiving the ISS</h2>

  - Tune to 145.800 MHz (Doppler shift is up to +/- 3.7 kHz). The Satellite Tracker can do this automatically, tracking Doppler shift: right click the ISS in its satellite table and select the SSTV transmitter.
  - Leave the mode on Auto, and turn on Auto save to keep all the images received during the pass.
  - Use an antenna with a clear view of the sky. An image needs about two minutes of reception, so images received at low elevation are often incomplete.

<h2>API</h2>

Full details of the API can be found in the Swagger documentation. Here is a quick example of how to set the mode to PD 180 on the first channel of the first device set:

    curl -X PATCH "http://127.0.0.1:8091/sdrangel/deviceset/0/channel/0/settings" -d '{"channelType": "SSTVDemod", "direction": 0, "SSTVDemodSettings": {"autoMode": 0, "mode": 6}}'

And how to get the state of the decoder, including the mode of the last image and the number of lines received:

    curl -X GET "http://127.0.0.1:8091/sdrangel/deviceset/0/channel/0/report"
