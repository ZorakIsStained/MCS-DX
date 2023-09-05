# tlc-strummer

This is an update based on the firmware for T. Chordstrum - USB MIDI chord strummer for Teensy LC. The update adds midi DIN output.

Original project: https://hackaday.io/project/25111-t-chordstrum

Chord strummer with a 12x3 keyboard based on the circle of fifths, like the Omnichord. For strumming the chords, a set of eight touch sensitive pads is used. 

Chords are selected as follows:

Columns from left to right selects the key
Db, Ab, Eb, Bb, F, C, G, D, A, E, B, F#

Rows select the kind of chord you play
top row     - major,
mid row     - minor,
bottom row  - 7th

Additional chord combinations are
top+bottom  - major 7th,
mid+bottom  - minor 7th,
top+mid     - diminished,
all three   - augmented or sustained depending on code


Using the Chord Strummer as a USB MIDI controller

Connect the T. Chordstrum via micro USB cable to a computer or other host device with a midi synth software installed and running.
Select "Teensy MIDI" as MIDI input in synth software if necessary.
Press and hold chord buttons and strum with a finger over the touch pads to play.

If using MIDI DIN, some other method to power the Teensy must be used. The micro is pretty flexible so it can be powered by a lithium battery without much additional circuitry.
