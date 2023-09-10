/////////////////////////////////////////////////////////////////////////
//                                                                     //
//            Midi Chord Strummer Deluxe by Rob McCarty, 2023          //
//         based on Teensy LC chord strummer by Johan Berglund         //
//                                                                     //
/////////////////////////////////////////////////////////////////////////

#define MIDI_CH 1            // MIDI channel
#define VELOCITY 64          // MIDI note velocity (64 for medium velocity, 127 for maximum)
#define NOTE_ON_CMD 0x90     // MIDI note on command for channel 1
#define NOTE_OFF_CMD 0x80    // MIDI note off command for channel 1
#define START_NOTE 48        // MIDI start note (48 is C3)
#define PADS 8               // number of touch electrodes
#define ANALOG_PIN A12       // pin for reading analog values, pin 26/A12
#define TOUCH_THR 2500       // threshold level for capacitive touch (lower is more sensitive)
#define serialPortRX 25      // not used, but defining so there aren't conflicts
#define serialPortTX 5      // MIDI DIN TX - teensy LC Serial1 tx pin options are 1, 4, 5, 24 for serial1, 
#define ADC_MIN 0
#define ADC_MAX 1023
#define BUFFER_SIZE 24      // size of sustain buffer; holds two full strums
#define DEBOUNCE 250        // time (ms) allowed between restrums 
#define RESOLUTION 12       // used in interpolation formula, needs to be max number of strum-able notes
#define CHECK_INTERVAL 4     // interval in ms for sensor check

unsigned long currentMillis = 0L;
unsigned long statusPreviousMillis = 0L;

// these buffers used to keep track of notes for sustain implementation
int noteBuffer[BUFFER_SIZE];
int susBuffer[BUFFER_SIZE];
int writeIndex = 0;
int readIndex = 0;

unsigned long sustain = 1500; // time between Note On and Note Off. TODO: add analog control of this value
unsigned long minSustain = 24;
unsigned long maxSustain = 8000;

int sensedValue[PADS]; // Array of capacitive sensor data
int strumRange = RESOLUTION*(PADS-1);
int intValue = 0;
int strumNote = 0;
int thresholdCheck = 0;
int lastPlayed = 0;

unsigned int noteOnCmd[3] = {NOTE_ON_CMD,0x00,VELOCITY};       // MIDI note on and note off message structures
unsigned int noteOffCmd[3] = {NOTE_OFF_CMD,0x00,VELOCITY};

byte colPin[12]          = {2,21,20,15,14,13, 6, 7, 8, 9,10,11};// teensy digital input pins for keyboard columns (just leave unused ones empty)
byte colNote[12]         = {1, 8, 3,10, 5, 0, 7, 2, 9, 4,11, 6};     // column to note number                                            
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

byte sensedNote;               // current reading
int noteNumber;                // calculated midi note number
int chord = 0;                 // chord key setting (base note of chord)
int chordType = 0;             // chord type (maj, min, 7th...)

int chordNote[8][12] = {        // chord notes for each pad/string
  {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 },  // silent
  { 0, 4, 7,12,16,19,24,28,31,36,40,43 },  // maj 
  { 0, 3, 7,12,15,19,24,27,31,36,39,43 },  // min 
  { 0, 3, 6,12,15,18,24,27,30,36,39,42 },  // dim 
  { 0, 4,10,12,16,22,24,28,34,36,40,46 },  // 7th 
  { 0, 4,11,12,16,23,24,28,35,36,40,47 },  // maj7
  { 0, 3,10,12,15,22,24,27,34,36,39,46 },  // m7  
  { 0, 2, 7,12,14,19,24,26,31,36,38,43 },  // sus2, uncomment this line and comment out aug line to enable 
  // { 0, 4, 8,12,16,20,24,28,32,36,40,44}   // aug  
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
    readKeyboard();                             // read keyboard input and replay active notes (if any) with new chording
    thresholdCheck = readSensors();             // check cap sensors
    if(sensedValue[thresholdCheck]>TOUCH_THR){	// if touch threshold reached, do interpolation, output value will be the note of the selected chord to play
      intValue = interpolateSensors(thresholdCheck); // output value is interpolated value that needs to be correlated to an array index
      strumNote = noteConvert(intValue);        // get the array index
      noteNumber = START_NOTE + chord + chordNote[chordType][strumNote]; // get the note
      if(noteNumber!=lastPlayed){               // don't retrigger by holding sensor
        if ((noteNumber < 128) && (noteNumber > -1) && (chordNote[chordType][strumNote] > -1)) {    // we don't want to send midi out of range or play silent notes
          lastPlayed = checkBuffAndNoteOn(noteNumber, currentMillis);  // perform checks and play note if debounce expired
        }
      }
    }
    else{
      lastPlayed = 0;         // sensor has been released, so clear variable
    }
	  checkSusBuffer(currentMillis);              // check to see if any notes need to be turned off
	  // updateSustain();                         // leave commented if hardware not implemented
    statusPreviousMillis = currentMillis;       // reset interval timing
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

int funcNoteOn(int note,unsigned long time){
  usbMIDI.sendNoteOn(note, VELOCITY, MIDI_CH);  // send Note On, USB MIDI
  midiNoteOn(note);                             // send Note On, DIN
  writeToBuffer(note,time);                     // add note to buffer of played notes, used for sustain checking
  return note;
}

void funcNoteOff(int note){
  usbMIDI.sendNoteOff(note, VELOCITY, MIDI_CH);  // send Note On, USB MIDI
  midiNoteOff(note);                             // send Note On, DIN
}

void writeToBuffer(int note, int time){          // writes note and time played to buffer
  noteBuffer[writeIndex] = note;                 // the buffer is periodically checked to see if note needs to be turned off
  susBuffer[writeIndex] = time;
  writeIndex++;
  if(writeIndex==BUFFER_SIZE){
    writeIndex = 0;
  }
}

bool isNoteInBuff(int note){           // checks to see if note is in buffer of sustained notes
  bool exist = false;
  for(int i = 0; i <BUFFER_SIZE; i++){
    if(note==noteBuffer[i]){
      exist=true;
    }
  }
  return exist;
}

bool isDebounceExpired(int note, long time){ // checks that the note debounce period has expired
  bool restrum = false;
  for(int i = 0; i <BUFFER_SIZE; i++){
    if(note==noteBuffer[i] && (time - susBuffer[i] > DEBOUNCE)){
      susBuffer[i] = time;                  // the return from this function is going to be used to retrigger the
      restrum=true;                         // note, so need to refresh the entry in the susBuffer, otherwise the
    }                                       // existing entry will turn note off prematurely
  }
  return restrum;
}

int checkBuffAndNoteOn(int note, long time){
  int a = 0;
  if(isNoteInBuff(note)){     // note is already in the buffer; the device already has note playing
    if(isDebounceExpired(note, time)){
      funcNoteOff(note);      // turn the note off so it can be turned back on/restrummed
      a = funcNoteOn(note, time);
    }  
  }
  else{
    a = funcNoteOn(note, time);  // note not in buffer, turn on
  }
  return a;         		// return value is used elsewhere to prevent spamming the same note if the electrode is held
}

void checkSusBuffer(unsigned long time){
  // check one entry per call. 24 items in buffer, 4ms check interval in main loop: 96 ms to get through entire buffer.
  if (time - susBuffer[readIndex] > sustain && susBuffer[readIndex]!=0){
    usbMIDI.sendNoteOff(noteBuffer[readIndex], VELOCITY, MIDI_CH);     // send note Off, USB MIDI
    midiNoteOff(noteBuffer[readIndex]);                                // send note Off, DIN
    noteBuffer[readIndex] = 0;		                           // remove note from buffer
    susBuffer[readIndex] = 0;					   // remove associated time from buffer so NoteOff isn't spammed
  }
  readIndex++;
  if(readIndex==BUFFER_SIZE){
    readIndex = 0;
  }
}

int readSensors(){   // read each capacitive sensor and return the index of the sensor with the highest value
  int highSense = 0;
  for(int i = 0;i<PADS;i++){						
    sensedValue[i] = touchRead(sensorPin[i]);
    if(sensedValue[i]>=sensedValue[highSense]){
      highSense = i;
    }		// at the end of this the sensor array is filled and the high value is determined
  }
  return highSense;
}

int interpolateSensors(int hSense){   // perform interpolation to figure out where on the strumpad is being pressed;
  switch(hSense){		      // because of the way the interpolation formula works, presses just at the ends 
    case 0:                           // won't get interpreted correctly, but if they're interpolated with the next 
      hSense = hSense+1;              // closest sensor, then the probability of getting the right note increases
    break;
    case PADS:
      hSense = hSense-1;
    break;
    default:
    break;
  }
  long a = sensedValue[hSense]*hSense+sensedValue[hSense-1]*(hSense-1)+sensedValue[hSense+1]*(hSense+1);
  long b = sensedValue[hSense]+sensedValue[hSense-1]+sensedValue[hSense+1];
  int interpolate = (RESOLUTION*a)/b;
  return interpolate;	// returns a value from 0 to RESOLUTION*(PADS-1)
}
 
// take the interpolated value and convert it to between 0 and 11
int noteConvert(int value){
  int lim = (RESOLUTION-1);
  int a = map(value,0,strumRange,0,lim);
  a = constrain(a,0,lim);                  // for some reason map() was returing values higher than the limit, this fixes it
  return a;
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
