<h1>Jog Wheel Feature Plugin</h1>

<h2>Introduction</h2>

The Jog Wheel tunes a channel in small steps, like the main tuning knob of an HF rig. It is meant for work where
dragging the channel marker on the spectrum is too coarse, such as finding a station in the passband of a linear
transponder satellite.

The wheel moves the channel's frequency offset (or the centre frequency of a device) by relative steps. Because the
Satellite Tracker's Doppler correction also moves the offset relative to its current value, the two combine: the
Doppler correction keeps running while you tune.

A second channel can be linked to the one tuned, so that the uplink follows the downlink. On an inverting transponder
(most linear satellites) the uplink moves the opposite way.

The wheel can be turned from the GUI, from hardware such as a rotary encoder over UDP, or through the Web API. Step
size and acceleration apply in each case, so a hardware knob feels the same as the one on screen.

<h3>With the Satellite Tracker</h3>

When the Satellite Tracker keeps a Tx channel on a transponder's uplink (see "Tx uplink channel" in its satellite table's menu),
it moves the uplink whenever the downlink is retuned, with the uplink's own Doppler correction. So set Tune to the downlink channel and Link to None
(the Satellite Tracker does this when you select a USB, LSB or CW transmitter from its menu, adding a Jog Wheel if there isn't one):
turning the wheel moves the downlink, and the tracker keeps the uplink in the same place in the transponder's passband.
You can also tune the uplink channel instead, and the tracker moves the downlink. If a link is set as well, the tracker uses the downlink's change,
so the uplink isn't moved twice.

The Link is for when the uplink isn't tuned by the Satellite Tracker, such as on a satellite it doesn't have uplink frequencies for.

<h2>Interface</h2>

<h3>Tune</h3>

What the wheel tunes:

  - A channel, such as `R0:1 SSBDemod`. Its offset from the device centre frequency is moved.
  - A device set, such as `R0 device centre`. The device's centre frequency is moved.

<h3>Link</h3>

A channel or device set that moves along with the one tuned, such as the modulator of a satellite uplink. Select
None for no link.

The combo box on the right sets the direction:

  - Inverted: the linked target moves the opposite way. Use this for inverting transponders: tuning the downlink up
    moves the uplink down by the same amount, so you stay on the same frequency in the passband.
  - Same: the linked target moves the same way, for non-inverting transponders.

<h3>Step</h3>

The frequency change for one detent of the wheel, from 1 Hz to 100 kHz.

<h3>Acc</h3>

Acceleration. When on, turning the wheel quickly multiplies the step: x2 above 20 detents per second, x5 above 40 and
x10 above 80. The speed resets when you pause or turn the other way, so single detents always move by one step. The
multiplier in use is shown next to the total change.

<h3>UDP, address and port</h3>

Listen for jog commands from hardware (see below). Use 127.0.0.1 to accept commands from this computer only, or
0.0.0.0 to accept them from the network, for example from a microcontroller on WiFi. The button turns red if the
port cannot be opened, and its tooltip gives the reason.

<h3>Frequencies</h3>

The frequency of the target (device centre frequency plus channel offset) and of the linked target, if any. They
follow changes made anywhere, including Doppler correction.

<h3>Total change</h3>

The total change made with the wheel since it was last reset, or since the target changed. Press 0 to reset it.
This is useful to come back to where you started, like the RIT display of a rig.

<h3>Knob</h3>

  - Drag round the centre of the knob to turn it. There are 40 detents per turn.
  - Turn the mouse wheel over the knob, one detent per notch.
  - Click the knob to give it focus, then use Up/Right and Down/Left for one detent, Page Up and Page Down for ten.

<h2>Hardware control over UDP</h2>

Each UDP datagram holds one or more text commands, separated by new lines or `;`. Upper or lower case is accepted.

| Command    | Action                                                                    |
|------------|---------------------------------------------------------------------------|
| `J<n>`     | Turn by n detents, E.g. `J1`, `J-3`. Step size and acceleration apply     |
| `<n>`      | Same as `J<n>`, E.g. `+1`, `-1`, `4`                                      |
| `H<n>`     | Move by n Hz, regardless of step size                                     |
| `S<n>`     | Set the step size to n Hz                                                 |
| `S`        | Next step size in the list, wrapping round (E.g. for an encoder's button) |
| `R`        | Reset the total change                                                    |

A rotary encoder only needs to send its count since the last datagram, for example every 20 ms while it is turning.
Acceleration works out the speed from the counts and their timing, so the hardware does not need to.

To try it from a shell:

```
echo "J5" | nc -u -w0 127.0.0.1 9988
echo "S" | nc -u -w0 127.0.0.1 9988
```

<h2>API</h2>

Full details of the API can be found in the Swagger documentation. Here is a selection.

To select the channel to tune and an inverted uplink:

    curl -X PATCH "http://127.0.0.1:8091/sdrangel/featureset/feature/0/settings" \
    -d '{"featureType": "JogWheel", "JogWheelSettings": { "target": "R0:0", "linkedTarget": "T1:0", "linkInverted": 1 }}'

To turn the wheel by 3 detents:

    curl -X POST "http://127.0.0.1:8091/sdrangel/featureset/feature/0/actions" \
    -d '{"featureType": "JogWheel", "JogWheelActions": { "jog": 3 }}'

To move by -500 Hz:

    curl -X POST "http://127.0.0.1:8091/sdrangel/featureset/feature/0/actions" \
    -d '{"featureType": "JogWheel", "JogWheelActions": { "deltaHz": -500 }}'

The report gives the frequency of the target and the linked target, and the total change:

    curl -X GET "http://127.0.0.1:8091/sdrangel/featureset/feature/0/report"
