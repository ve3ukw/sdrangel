<h1>Address Book Feature Plugin</h1>

<h2>Introduction</h2>

The Address Book feature stores frequencies and the settings that go with them, such as mode, bandwidth, repeater shift and CTCSS tone,
and tunes a receive channel to them.

Entries are saved with the feature's settings, so they are part of configurations.

<h2>Interface</h2>

<h3>1: Channel</h3>

Selects the receive channel that is tuned when an entry is tuned.

<h3>2: Group</h3>

Only shows entries in the selected group. Select All to show every entry.

<h3>3: Entries table</h3>

Lists the entries. Click a column header to sort by that column. Double click an entry to tune the channel to it.

* Name - Name of the entry. E.g. a callsign, repeater or satellite name.
* Group - Group the entry is filed under.
* Freq (MHz) - Receive frequency in MHz.
* Mode - Mode, as free text. E.g. NFM, USB or SSTV.
* Channel - Type of channel the entry can be tuned on. Any, if the entry can be tuned on any channel type.
* BW (kHz) - RF bandwidth in kHz. Blank if the channel's bandwidth is left unchanged.
* Tx Offset (MHz) - Transmit frequency minus receive frequency in MHz, for repeaters and satellites.
* CTCSS - CTCSS tone in Hz.
* Notes - First line of the notes. Hover over the cell to see all of the notes.

<h3>4: Tune</h3>

Tunes the channel to the selected entry:

* If the selected channel is a transmit channel, or its type differs from the entry's channel type, and its link button is on,
  the receive channel linked to it is tuned instead (E.g. selecting an NFM modulator tunes its linked NFM demodulator).
* Otherwise, if the entry's channel type is not Any and differs from the selected channel's type, the entry is not tuned and an error is displayed.
* If the frequency is within the device's current bandwidth, only the channel's frequency offset is changed.
  Otherwise, the device's center frequency is changed, keeping the channel's frequency offset.
* If a bandwidth is set, the channel's RF bandwidth is set.
* The channel can be on a single Rx device or on an Rx stream of a MIMO device (E.g. PlutoSDR MIMO).
* The CTCSS tone of an NFM demodulator is set to the entry's CTCSS tone (`--` if the entry has none).
* If the channel's link button is on (SSB and NFM demodulators), the channels linked to it are set to the same frequency.
  Linked NFM modulators also get the entry's Tx offset as their repeater shift, and the entry's CTCSS tone, with CTCSS
  switched on if the entry has a tone and off if it doesn't.

<h3>5: Add</h3>

Opens a dialog to add an entry.

<h3>6: Add from channel</h3>

Opens a dialog to add an entry, with the frequency, channel type and RF bandwidth of the selected channel filled in.

<h3>7: Edit</h3>

Opens a dialog to edit the selected entry.

<h3>8: Delete</h3>

Deletes the selected entry.
