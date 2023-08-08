/////////////////////////////////////////////////////////////////////////
//                                                                     //
//   Teensy LC chord strummer by Johan Berglund, April 2017            //
//       Modified August 2023 by Rob McCarty                            //
//                                                                     //
/////////////////////////////////////////////////////////////////////////

#include <CircularBuffer.h>

#define MIDI_CH 1            // MIDI channel
#define VELOCITY 64          // MIDI note velocity (64 for medium velocity, 127 for maximum)
#define NOTE_ON_CMD 0x90     // MIDI note on command for channel 1
#define NOTE_OFF_CMD 0x80    // MIDI note off command for channel 1
#define START_NOTE 48        // MIDI start note (48 is C3)
#define PADS 8               // number of touch electrodes
#define ANALOG_PIN A12       // pin for reading analog values, pin 26/A12
#define TOUCH_THR 1500       // threshold level for capacitive touch (lower is more sensitive)
#define serialPortRX 25      // not used, but defining so there aren't conflicts
#define serialPortTX 5      // MIDI DIN TX - teensy LC Serial1 tx pin options are 1, 4, 5, 24 for serial1, 
#define ADC_MIN 0
#define ADC_MAX 1023
#define BUFFER_SIZE 32      // size of sustain buffer
#define DEBOUNCE 500        // time (ms) allowed between restrums 

#define CHECK_INTERVAL 4     // interval in ms for sensor check

unsigned long currentMillis = 0L;
unsigned long statusPreviousMillis = 0L;

// these buffers used to keep track of notes for sustain implementation
CircularBuffer<int, BUFFER_SIZE> noteBuffer;
CircularBuffer<int, BUFFER_SIZE> susBuffer;
unsigned long sustain = 1500; // time between Note On and Note Off. TODO: add analog control of this value
unsigned long minSustain = 24;
unsigned long maxSustain = 8000;


unsigned int noteOnCmd[3] = {NOTE_ON_CMD,0x00,VELOCITY};       // MIDI note on and note off message structures
unsigned int noteOffCmd[3] = {NOTE_OFF_CMD,0x00,VELOCITY};

byte colPin[12]          = {2,21,20,15,14,13,6,7,8,9,10,11};// teensy digital input pins for keyboard columns (just leave unused ones empty)
byte colNote[12]         = {1,8,3,10,5,0,7,2,9,4,11,6};     // column to note number                                            
                                                            // column setup for omnichord style (circle of fifths)
                                                            // chord    Db, Ab, Eb, Bb,  F,  C,  G,  D,  A,  E,  B, F#
                                                            // col/note  1,  8,  3, 10,  5,  0,  7,  2,  9,  4, 11,  6
                                                            // for chromatic order, C to B, straight order 0 to 11
                                                            // original tlc strummer design only use pins 5 through 12 (chords Bb to B)

byte rowPin[3]           = {3,4,12};                         // teensy output pins for keyboard rows

                                                            // chord type   maj, min, 7th
                                                            // row            0,   1,   2
                                                            
                                                            // chordType 0 to 7, from binary row combinations
                                                            // 0 0 0 silent
                                                            // 0 0 1 maj
                                                            // 0 1 0 min
                                                            // 0 1 1 dim  (maj+min keys)
                                                            // 1 0 0 7th
                                                            // 1 0 1 maj7 (maj+7th keys)
                                                            // 1 1 0 m7   (min+7th keys)
                                                            // 1 1 1 aug  (maj+min+7th)

byte sensorPin[8]       = {18,19,17,22,16,23,1,0};          // teensy lc touch input pins
byte activeNote[8]      = {0,0,0,0,0,0,0,0};                // keeps track of active notes

byte sensedNote;               // current reading
int noteNumber;                // calculated midi note number
int chord = 0;                 // chord key setting (base note of chord)
int chordType = 0;             // chord type (maj, min, 7th...)

int chordNote[8][8] = {        // chord notes for each pad/string
  {-1,-1,-1,-1,-1,-1,-1,-1 },  // silent
  { 0, 4, 7,12,16,19,24,28 },  // maj 
  { 0, 3, 7,12,15,19,24,27 },  // min 
  { 0, 3, 6, 9,12,15,18,21 },  // dim 
  { 0, 4, 7,10,12,16,19,22 },  // 7th 
  { 0, 4, 7,11,12,16,19,23 },  // maj7
  { 0, 3, 7,10,12,15,19,22 },  // m7  
  { 0, 5, 7,12,17,19,24,29 },  // sus, uncomment this line and comment out aug line to enable 
  // { 0, 4, 8,12,16,20,24,28 }   // aug  
};

// SETUP
void setup() {
  Serial1.setRX(serialPortRX);
  Serial1.setTX(serialPortTX);
  Serial1.begin(31250);
  for (int i = 0; i < 12; i++) {
     pinMode(colPin[i],INPUT_PULLUP);
  }
    for (int i = 0; i < 3; i++) {
     pinMode(rowPin[i],OUTPUT);
     digitalWrite(rowPin[i],LOW);
  }
}

// MAIN LOOP
void loop() {
  currentMillis = millis();
  if ((unsigned long)(currentMillis - statusPreviousMillis) >= CHECK_INTERVAL) {
    readKeyboard();                                                      // read keyboard input and replay active notes (if any) with new chording
    for (int scanSensors = 0; scanSensors < PADS; scanSensors++) {       // scan sensors for changes and send note on/off accordingly
      sensedNote = (touchRead(sensorPin[scanSensors]) > TOUCH_THR);      // read touch pad/pin/electrode/string/whatever
      if (sensedNote != activeNote[scanSensors]) {
        noteNumber = START_NOTE + chord + chordNote[chordType][scanSensors];
        if ((noteNumber < 128) && (noteNumber > -1) && (chordNote[chordType][scanSensors] > -1)) {    // we don't want to send midi out of range or play silent notes
          if (sensedNote){
              checkBuffAndNoteOn(noteNumber, currentMillis);  // send Note On with time, buffer will be checked so that notes already playing will restrum
          } else {
              // usbMIDI.sendNoteOff(noteNumber, VELOCITY, MIDI_CH);     // send note Off, USB MIDI
              // midiNoteOff(noteNumber);                                // send note Off, DIN
          }
        }  
        activeNote[scanSensors] = sensedNote;         
      }  
    }
	checkSusBuffer(currentMillis);                                   // check to see if any notes need to be turned off
	// updateSustain();                                              // leave commented if hardware not implemented
    statusPreviousMillis = currentMillis;                          // reset interval timing
  }
}
// END MAIN LOOP

// Check chord keyboard, if changed shut off any active notes and replay with new chord
void readKeyboard() {
  int readChord = 0;
  int readChordType = 0;
  for (int row = 0; row < 3; row++) {     // scan keyboard rows
    enableRow(row);                       // set current row low
    for (int col = 0; col < 12; col++) {  // scan keyboard columns
      if (!digitalRead(colPin[col])) {    // is scanned pin low (active)?
        readChord = colNote[col];         // set chord base note
        readChordType |= (1 << row);      // set row bit in chord type
      }
    }
  }  
  if ((readChord != chord) || (readChordType != chordType)) { // have the values changed since last scan?
  // TODO: sustain implementation broke the part of this that turns off active notes, consider looping through buffer and turning off notes when chords change.
    for (int i = 0; i < PADS; i++) {
       noteNumber = START_NOTE + chord + chordNote[chordType][i];
       if ((noteNumber < 128) && (noteNumber > -1) && (chordNote[chordType][i] > -1)) {      // we don't want to send midi out of range or play silent notes
         if (activeNote[i]) {
          // usbMIDI.sendNoteOff(noteNumber, VELOCITY, MIDI_CH); // send Note Off, USB MIDI
          // midiNoteOff(noteNumber);                             // send Note Off, DIN
         }
       }
    }
    for (int i = 0; i < PADS; i++) {
      noteNumber = START_NOTE + readChord + chordNote[readChordType][i];
      if ((noteNumber < 128) && (noteNumber > -1) && (chordNote[readChordType][i] > -1)) {    // we don't want to send midi out of range or play silent notes
        if (activeNote[i]) {
          checkBuffAndNoteOn(noteNumber,currentMillis);  // send Note On with time
        }
      }
    }
    chord = readChord;
    chordType = readChordType;
  }
}

// Set selected row low (active), others to Hi-Z
void enableRow(int row) {
  for (int rc = 0; rc < 3; rc++) {
    if (row == rc) {
      pinMode(rowPin[rc], OUTPUT);
      digitalWrite(rowPin[rc], LOW);
    } else {
      digitalWrite(rowPin[rc], HIGH);
      pinMode(rowPin[rc], INPUT); // Put to Hi-Z for safety against shorts
    }
  }
  delayMicroseconds(30); // wait before reading ports (let ports settle after changing)
}

void updateSustain(){
	unsigned long inputVal = analogRead(ANALOG_PIN);
	sustain = map(inputVal,0,ADC_MAX,minSustain,maxSustain);
}

void funcNoteOn(int note,unsigned long time){
	usbMIDI.sendNoteOn(note, VELOCITY, MIDI_CH);  // send Note On, USB MIDI
  midiNoteOn(note);                             // send Note On, DIN
	noteBuffer.push(note);				 		  // add note to end of buffer
	susBuffer.push(time);						  // add time to end of buffer (aligned to note)
}

void funcNoteOff(int note){
	usbMIDI.sendNoteOff(note, VELOCITY, MIDI_CH);  // send Note On, USB MIDI
  midiNoteOff(note);                             // send Note On, DIN
}

bool findNote(int note, unsigned long time){           // checks to see if note is in buffer and enough time has passed to restrum
  bool restrum;
  for(int i = 0; i <noteBuffer.size(); i++){
    if(note==noteBuffer[i] && (time - susBuffer[i] > DEBOUNCE)){
      restrum=true;
    }
  }
  return restrum;
}

void checkBuffAndNoteOn(int note, long time){
  if(findNote(note, time)){                       // note is already in the buffer; the device already has note playing
    funcNoteOff(note);                      // turn the note off so it can be turned back on/restrummed
  }
  funcNoteOn(note, time);                         // send Note On commands
}

void checkSusBuffer(unsigned long time){
	// check oldest (lowest indexed) note in buffer to see if it needs to be turned off
	// newer notes are always added to end of buffer while older notes are removed from the front, so only the first entry needs to be checked on each pass.
	// 32 items in buffer, 4ms check interval in main loop: up to 128 ms to get through entire buffer.
	if (time - susBuffer[0] > sustain && !noteBuffer.isEmpty()){
		usbMIDI.sendNoteOff(noteBuffer[0], VELOCITY, MIDI_CH);     // send note Off, USB MIDI
		midiNoteOff(noteBuffer[0]);                                // send note Off, DIN
		noteBuffer.shift();										   // remove note from buffer
		susBuffer.shift();										   // remove associated time from buffer to keep things aligned
	}
}

// Send Note On commands over serial port. Channel and Velocity set above.
void midiNoteOn(unsigned int note){
  noteOnCmd[1] = note;
  Serial1.write(noteOnCmd[0]);
  Serial1.write(noteOnCmd[1]);
  Serial1.write(noteOnCmd[2]);
}

// Send Note Off commands over serial port. Channel and Velocity set above.
void midiNoteOff(unsigned int note){
  noteOffCmd[1] = note;
  Serial1.write(noteOffCmd[0]);
  Serial1.write(noteOffCmd[1]);
  Serial1.write(noteOffCmd[2]);
}
