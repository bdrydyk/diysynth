/* 
 *    usbMIDI2CV_MC 
 *    Copyright (C) 2018  Larry McGovern
 *  
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License <http://www.gnu.org/licenses/> for more details.
 */
 
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <Bounce2.h>

// OLED I2C is used on pins 5 and 6

#define DAC1      9
#define DAC2      8
#define DAC3      7
#define DAC4      6
#define GATE_CH0  2
#define GATE_CH1  3
#define GATE_CH2  4

#define ENC_A   14
#define ENC_B   15
#define ENC_BTN 16


#define GATE(CH) (CH==0 ? GATE_CH0 : (CH==1 ? GATE_CH1 : GATE_CH2))

// Note CH0: DAC1, A
// Note CH1: DAC1, B
// Note CH2: DAC2, A
#define NOTE_DAC(CH) (CH==2 ? DAC2 : DAC1)
#define NOTE_AB(CH)  (CH==1 ? 1 : 0)

// Vel CH0: DAC2, B
// Vel CH1: DAC3, A
// Vel CH2: DAC3, B
#define VEL_DAC(CH) (CH==0 ? DAC2 : DAC3)
#define VEL_AB(CH)  (CH==1 ? 0 : 1)

#define PITCH_DAC DAC4
#define PITCH_AB  0
#define CC_DAC    DAC4
#define CC_AB     1

#define OLED_RESET 17
Adafruit_SSD1306 display(OLED_RESET);

int encoderPos, encoderPosPrev;
Bounce encButton = Bounce(); 

enum Menu {
  SETTINGS,
  NOTE_PRIORITY,
  NOTE_PRIORITY_SET_CH,
  GATE_TRIG,
  GATE_TRIG_SET_CH,
  PITCH_BEND_SET_CH,
  CC_SET_CH,
  SCALE_FACTOR,
  SCALE_FACTOR_SET_CH
} menu;

char notePriority[] = "TTT";
char gateTrig[] = "GGG";
float sfAdj[3];

uint8_t pitchBendChan;
uint8_t ccChan;

// EEPROM Addresses
#define ADDR_NOTE_PRIORITY 0
#define ADDR_GATE_TRIG     3
#define ADDR_PITCH_BEND    6
#define ADDR_CC            7
#define ADDR_SF_ADJUST     8

bool highlightEnabled = false;  // Flag indicating whether highighting should be enabled on menu
#define HIGHLIGHT_TIMEOUT 20000  // Highlight disappears after 20 seconds.  Timer resets whenever encoder turned or button pushed
unsigned long int highlightTimer = 0;  

void setup()
{
 pinMode(GATE_CH0, OUTPUT);
 pinMode(GATE_CH1, OUTPUT);
 pinMode(GATE_CH2, OUTPUT);
 pinMode(DAC1, OUTPUT);
 pinMode(DAC2, OUTPUT);
 pinMode(DAC3, OUTPUT);
 pinMode(DAC4, OUTPUT);
 pinMode(ENC_A, INPUT_PULLUP);
 pinMode(ENC_B, INPUT_PULLUP);
 pinMode(ENC_BTN,INPUT_PULLUP);
  
 digitalWrite(GATE_CH0,LOW);
 digitalWrite(GATE_CH1,LOW);
 digitalWrite(GATE_CH2,LOW);
 digitalWrite(DAC1,HIGH);
 digitalWrite(DAC2,HIGH);
 digitalWrite(DAC3,HIGH);
 digitalWrite(DAC4,HIGH);


 Serial.begin(9600);
 SPI.begin();

  usbMIDI.setHandleNoteOn(myNoteOn);
  usbMIDI.setHandleNoteOff(myNoteOff);
  usbMIDI.setHandleAfterTouchPoly(myAfterTouchPoly);
  usbMIDI.setHandleControlChange(myControlChange);
  usbMIDI.setHandleProgramChange(myProgramChange);
  usbMIDI.setHandleAfterTouchChannel(myAfterTouchChannel);
  usbMIDI.setHandlePitchChange(myPitchChange);
  // Only one of these System Exclusive handlers will actually be
  // used.  See the comments below for the difference between them.
  usbMIDI.setHandleSystemExclusive(mySystemExclusiveChunk);
  usbMIDI.setHandleSystemExclusive(mySystemExclusive); 
  usbMIDI.setHandleTimeCodeQuarterFrame(myTimeCodeQuarterFrame);
  usbMIDI.setHandleSongPosition(mySongPosition);
  usbMIDI.setHandleSongSelect(mySongSelect);
  usbMIDI.setHandleTuneRequest(myTuneRequest);
  usbMIDI.setHandleClock(myClock);
  usbMIDI.setHandleStart(myStart);
  usbMIDI.setHandleContinue(myContinue);
  usbMIDI.setHandleStop(myStop);
  usbMIDI.setHandleActiveSensing(myActiveSensing);
  usbMIDI.setHandleSystemReset(mySystemReset);
  // This generic System Real Time handler is only used if the
  // more specific ones are not set.
  usbMIDI.setHandleRealTimeSystem(myRealTimeSystem);

  
 display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // OLED I2C Address, may need to change for different device,
                                            // Check with I2C_Scanner

 //Wire.setClock(100000L);  // Uncomment to slow down I2C speed

 // Read Settings from EEPROM
 for (int i=0; i<3; i++) {
   notePriority[i] = (char)EEPROM.read(ADDR_NOTE_PRIORITY+i);
   gateTrig[i] = (char)EEPROM.read(ADDR_GATE_TRIG+i);
   EEPROM.get(ADDR_SF_ADJUST+i*sizeof(float), sfAdj[i]);

  // Set defaults if EEPROM not initialized
  if (notePriority[i] != 'T' && notePriority[i] != 'B' && notePriority[i] != 'L') notePriority[i] = 'T';
  if (gateTrig[i] != 'G' && gateTrig[i] != 'T') gateTrig[i] = 'G';
  if ((sfAdj[i] < 0.9f) || (sfAdj[i] > 1.1f) || isnan(sfAdj[i])) sfAdj[i] = 1.0f; 
 }
  
 pitchBendChan = EEPROM.read(ADDR_PITCH_BEND);
 ccChan = EEPROM.read(ADDR_CC);

// Set defaults if EEPROM not initialized
 if (pitchBendChan > 2) pitchBendChan = 0;
 if (ccChan > 2) ccChan = 0;

 encButton.attach(ENC_BTN);
 encButton.interval(5);  // interval in ms

 menu = SETTINGS;
 updateSelection();

 Serial.print("Setup Done");
}


unsigned long trigTimer[3] = {0};
bool notes[3][88] = {0}, initial_loop = 1; 
int8_t noteOrder[3][10] = {0}, orderIndx[3] = {0};

void loop()
{
  int8_t noteMsg, velocity, channel, d2, i;

  updateEncoderPos();  
  encButton.update();

  usbMIDI.read();

 if (encButton.fell()) {  
   if (initial_loop == 1) {
     initial_loop = 0;  // Ignore first push after power-up
    }
   else {
     updateMenu();
    }
   }

  // Check if highlighting timer expired, and remove highlighting if so
  if (highlightEnabled && ((millis() - highlightTimer) > HIGHLIGHT_TIMEOUT)) {  
    highlightEnabled = false;
    menu = SETTINGS;    // Return to top level menu
    updateSelection();  // Refresh screen without selection highlight
  }
  
  for (i=0;i<3;i++) {
    if ((trigTimer[i] > 0) && (millis() - trigTimer[i] > 20) && gateTrig[i] == 'T') { 
      digitalWrite(GATE(i),LOW); // Set trigger low after 20 msec 
      trigTimer[i] = 0;  
    }
  }
}


void commandTopNote(int channel)
{
  int topNote = 0;
  bool noteActive = false;
 
  for (int i=0; i<88; i++)
  {
    if (notes[channel][i]) {
      topNote = i;
      noteActive = true;
    }
  }

  if (noteActive) 
    commandNote(topNote, channel);
  else // All notes are off, turn off gate
    digitalWrite(GATE(channel),LOW);
}

void commandBottomNote(int channel)
{
  int bottomNote = 0;
  bool noteActive = false;
 
  for (int i=87; i>=0; i--)
  {
    if (notes[channel][i]) {
      bottomNote = i;
      noteActive = true;
    }
  }

  if (noteActive) 
    commandNote(bottomNote, channel);
  else // All notes are off, turn off gate
    digitalWrite(GATE(channel),LOW);
}

void commandLastNote(int channel)
{
  int8_t noteIndx;
  
  for (int i=0; i<10; i++) {
    noteIndx = noteOrder[channel][ mod(orderIndx[channel]-i, 10) ];
    if (notes[channel][noteIndx]) {
      commandNote(noteIndx,channel);
      return;
    }
  }
  digitalWrite(GATE(channel),LOW);  // All notes are off
}

// Rescale 88 notes to 4096 mV:
//    noteMsg = 0 -> 0 mV 
//    noteMsg = 87 -> 4096 mV
// DAC output will be (4095/87) = 47.069 mV per note, and 564.9655 mV per octive
// Note that DAC output will need to be amplified by 1.77X for the standard 1V/octive 
#define NOTE_SF 47.069f 

void commandNote(int noteMsg, int channel) {
  digitalWrite(GATE(channel),HIGH);
  trigTimer[channel] = millis();
  
  unsigned int mV = (unsigned int) ((float) noteMsg * NOTE_SF * sfAdj[channel] + 0.5);   
  setVoltage(NOTE_DAC(channel), NOTE_AB(channel), 1, mV);  
}


// commandVel -- Command velocity DAC
void commandVel(int velocity, int8_t channel) {
  // velocity range from 0 to 4095 mV  Left shift d2 by 5 to scale from 0 to 4095, 
  // and choose gain = 2X
  setVoltage(VEL_DAC(channel), VEL_AB(channel), 1, velocity<<5);  
}

// setVoltage -- Set DAC voltage output
// dacpin: chip select pin for DAC.  Note and velocity on DAC1, pitch bend and CC on DAC2
// channel: 0 (A) or 1 (B).  
// gain: 0 = 1X, 1 = 2X.  
// mV: integer 0 to 4095.  If gain is 1X, mV is in units of half mV (i.e., 0 to 2048 mV).
// If gain is 2X, mV is in units of mV

void setVoltage(int dacpin, bool channel, bool gain, unsigned int mV)
{
  Serial.printf("Set voltage called: %u\n", mV);
  int command = channel ? 0x9000 : 0x1000;

  command |= gain ? 0x0000 : 0x2000;
  command |= (mV & 0x0FFF);
  
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(dacpin,LOW);
  SPI.transfer(command>>8);
  SPI.transfer(command&0xFF);
  digitalWrite(dacpin,HIGH);
  SPI.endTransaction();
}


void updateEncoderPos() {
    static int encoderA, encoderB, encoderA_prev;   

    encoderA = digitalRead(ENC_A); 
    encoderB = digitalRead(ENC_B);
      
    if((!encoderA) && (encoderA_prev)){ // A has gone from high to low 
      if (highlightEnabled) { // Update encoder position
        encoderPosPrev = encoderPos;
        encoderB ? encoderPos++ : encoderPos--;  
      }
      else { 
        highlightEnabled = true;
        encoderPos = 0;  // Reset encoder position if highlight timed out
        encoderPosPrev = 0;
      }
      highlightTimer = millis(); 
      updateSelection();
    }
    encoderA_prev = encoderA;     
}

int setCh;

void updateMenu() {  // Called whenever button is pushed

  if (highlightEnabled) { // Highlight is active, choose selection
    switch (menu) {
      case SETTINGS:
        switch (mod(encoderPos,5)) {
          case 0: 
            menu = NOTE_PRIORITY;
            break;
          case 1: 
            menu = GATE_TRIG;
            break;
          case 2: 
            menu = PITCH_BEND_SET_CH;
            break;
          case 3: 
            menu = CC_SET_CH;
            break;
          case 4: 
            menu = SCALE_FACTOR;
            break;
        }
        break;
        
      case NOTE_PRIORITY:
        setCh = mod(encoderPos,4);
        switch (setCh) {
          case 0: 
          case 1: 
          case 2: 
            menu = NOTE_PRIORITY_SET_CH;
            break;
          case 3: 
            menu = SETTINGS;
            break;
        }
        break;
        
      case NOTE_PRIORITY_SET_CH:  // Save note priority setting to EEPROM
        menu = NOTE_PRIORITY;
        EEPROM.write(ADDR_NOTE_PRIORITY+setCh, notePriority[setCh]);
        break;
        
      case GATE_TRIG:
        setCh = mod(encoderPos,4);
        switch (setCh) {
          case 0: 
          case 1: 
          case 2: 
            menu = GATE_TRIG_SET_CH;
            break;
          case 3: 
            menu = SETTINGS;
            break;
        }
        break;
  
      case GATE_TRIG_SET_CH:  // Save gate/trigger setting to EEPROM
        menu = GATE_TRIG;
        EEPROM.write(ADDR_GATE_TRIG+setCh, gateTrig[setCh]);
        break;
  
      case PITCH_BEND_SET_CH:  // Save pitch bend setting to EEPROM
        menu = SETTINGS;
        EEPROM.write(ADDR_PITCH_BEND, pitchBendChan);
        break;
        
      case CC_SET_CH:  // Save pitch bend setting to EEPROM
        menu = SETTINGS;
        EEPROM.write(ADDR_CC, ccChan);
        break;

      case SCALE_FACTOR:
        setCh = mod(encoderPos,4);
        switch (setCh) {
          case 0: 
          case 1: 
          case 2: 
            menu = SCALE_FACTOR_SET_CH;
            break;
          case 3: 
            menu = SETTINGS;
            break;
        }
        break;
        
      case SCALE_FACTOR_SET_CH: // Save scale factor to EEPROM
        menu = SCALE_FACTOR;
        EEPROM.put(ADDR_SF_ADJUST+setCh*sizeof(float), sfAdj[setCh]);
        break;      
    }
  }
  else { // Highlight wasn't visible, reinitialize highlight timer
    highlightTimer = millis();
    highlightEnabled = true;
  }
  encoderPos = 0;  // Reset encoder position
  encoderPosPrev = 0;
  updateSelection(); // Refresh screen
}

void updateSelection() { // Called whenever encoder is turned
  display.clearDisplay();
  switch (menu) {
    case PITCH_BEND_SET_CH:
      pitchBendChan = mod(encoderPos, 3);
      // No break statement, continue through next case
    case CC_SET_CH:
      if (menu == CC_SET_CH) ccChan = mod(encoderPos, 3);
      // No break statement, continue through next case
    case SETTINGS:
      display.setCursor(0,0); 
      display.setTextColor(WHITE,BLACK);
      display.print(F("SETTINGS"));
      display.setCursor(0,16);
      
      if (menu == SETTINGS) setHighlight(0,5);
      display.print(F("Note Priority "));
      display.println(notePriority);
      
      if (menu == SETTINGS) setHighlight(1,5);
      display.print(F("Gate/Trig     "));
      display.println(gateTrig);
      
      if (menu == SETTINGS) setHighlight(2,5);
      display.print(F("Pitch Bend    "));
      if (menu == PITCH_BEND_SET_CH) display.setTextColor(BLACK,WHITE);
      display.print(pitchBendChan+1);
      display.println(F("  "));
      
      if (menu == SETTINGS) setHighlight(3,5);
      else display.setTextColor(WHITE,BLACK);
      display.print(F("CC            "));
      if (menu == CC_SET_CH) display.setTextColor(BLACK,WHITE);
      display.print(ccChan+1);
      display.println(F("  "));

      if (menu == SETTINGS) setHighlight(4,5);
      else display.setTextColor(WHITE,BLACK);
      display.println(F("Scale Factor     "));
      
      break;
      
    case NOTE_PRIORITY_SET_CH:
      switch (mod(encoderPos, 3)) {
        case 0:
          notePriority[setCh] = 'T';  // Top note
          break;
        case 1:
          notePriority[setCh] = 'B';  // Bottom note
          break;
        case 2: 
          notePriority[setCh] = 'L';  // Last note
          break;
      }
      // No break statement, continue through next case
    case NOTE_PRIORITY:
    
      display.setCursor(0,0); 
      display.setTextColor(WHITE,BLACK);
      display.print(F("SET NOTE PRIORITY"));
      display.setCursor(0,16);
      
      if (menu == NOTE_PRIORITY) setHighlight(0,4);
      display.print(F("Channel 1: "));
      if ((menu == NOTE_PRIORITY_SET_CH) && setCh == 0) display.setTextColor(BLACK,WHITE);
      display.println(notePriority[0]);
      
      if (menu == NOTE_PRIORITY) setHighlight(1,4);
      else display.setTextColor(WHITE,BLACK);
      display.print(F("Channel 2: "));
      if ((menu == NOTE_PRIORITY_SET_CH) && setCh == 1) display.setTextColor(BLACK,WHITE);
      display.println(notePriority[1]);
      
      if (menu == NOTE_PRIORITY) setHighlight(2,4);
      else display.setTextColor(WHITE,BLACK);
      display.print(F("Channel 3: ")); 
      if ((menu == NOTE_PRIORITY_SET_CH) && setCh == 2) display.setTextColor(BLACK,WHITE);
      display.println(notePriority[2]); 
      
      if (menu == NOTE_PRIORITY) setHighlight(3,4);
      else display.setTextColor(WHITE,BLACK);
      display.println(F("Return           "));       
      break;  
      
    case GATE_TRIG_SET_CH:
      switch (mod(encoderPos, 2)) {
        case 0:
          gateTrig[setCh] = 'G';  // Gate
          break;
        case 1:
          gateTrig[setCh] = 'T';  // Trigger
          break;
      }
      // No break statement, continue through next case
    case GATE_TRIG:
      display.setCursor(0,0); 
      display.setTextColor(WHITE,BLACK);
      display.print(F("SET GATE/TRIGGER"));
      display.setCursor(0,16);
      
      if (menu == GATE_TRIG) setHighlight(0,4);
      display.print(F("Channel 1: "));
      if ((menu == GATE_TRIG_SET_CH) && setCh == 0) display.setTextColor(BLACK,WHITE);
      display.println(gateTrig[0]);
      
      if (menu == GATE_TRIG) setHighlight(1,4);
      else display.setTextColor(WHITE,BLACK);
      display.print(F("Channel 2: "));
      if ((menu == GATE_TRIG_SET_CH) && setCh == 1) display.setTextColor(BLACK,WHITE);
      display.println(gateTrig[1]);
            
      if (menu == GATE_TRIG) setHighlight(2,4);
      else display.setTextColor(WHITE,BLACK);
      display.print(F("Channel 3: "));  
      if ((menu == GATE_TRIG_SET_CH) && setCh == 2) display.setTextColor(BLACK,WHITE);
      display.println(gateTrig[2]);
            
      if (menu == GATE_TRIG) setHighlight(3,4);
      else display.setTextColor(WHITE,BLACK);
      display.print(F("Return      "));  
      break;
      
    case SCALE_FACTOR_SET_CH:
        if ((encoderPos > encoderPosPrev) && (sfAdj[setCh] < 1.1))
            sfAdj[setCh] += 0.001f;
        else if ((encoderPos < encoderPosPrev) && (sfAdj[setCh] > 0.9))
            sfAdj[setCh] -= 0.001f;           
      // No break statement, continue through next case
    case SCALE_FACTOR:
      display.setCursor(0,0); 
      display.setTextColor(WHITE,BLACK);
      display.print(F("SET SCALE FACTOR"));   
      display.setCursor(0,16);
      
      if (menu == SCALE_FACTOR) setHighlight(0,4);
      display.print(F("Channel 1: "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 0) display.setTextColor(BLACK,WHITE);
      display.println(sfAdj[0],3);
      
      if (menu == SCALE_FACTOR) setHighlight(1,4);
      else display.setTextColor(WHITE,BLACK);
      display.print(F("Channel 2: "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 1) display.setTextColor(BLACK,WHITE);
      display.println(sfAdj[1],3);
            
      if (menu == SCALE_FACTOR) setHighlight(2,4);
      else display.setTextColor(WHITE,BLACK);
      display.print(F("Channel 3: "));  
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 2) display.setTextColor(BLACK,WHITE);
      display.println(sfAdj[2],3);
            
      if (menu == SCALE_FACTOR) setHighlight(3,4);
      else display.setTextColor(WHITE,BLACK);
      display.print(F("Return      "));  
       
  }
  display.display(); 
}

void setHighlight(int menuItem, int numMenuItems) {
  if ((mod(encoderPos, numMenuItems) == menuItem) &&  highlightEnabled ) {
    display.setTextColor(BLACK,WHITE);
  }
  else {
    display.setTextColor(WHITE,BLACK);
  }
}

int mod(int a, int b)
{
    int r = a % b;
    return r < 0 ? r + b : r;
}




void myNoteOn(byte channel, byte note, byte velocity) {
  // When using MIDIx4 or MIDIx16, usbMIDI.getCable() can be used
  // to read which of the virtual MIDI cables received this message.
  
  Serial.print("Note On, ch=");
  Serial.print(channel, DEC);
  Serial.print(", note=");
  Serial.print(note, DEC);
  Serial.print(", velocity=");
  Serial.println(velocity, DEC);

  if (channel > 2) return;  // Only channel 0,1,2 supported
  
  //if ((note < 0) || (note > 87)) break;  // Only 88 notes of keyboard are supported

  if (velocity == 0)  {
    notes[channel][note] = false;
  }
  else {
    notes[channel][note] = true;
    commandVel(velocity, channel); 
  }


  if (notePriority[channel] == 'T') // Top note priority
    commandTopNote(channel);
  else if (notePriority[channel] == 'B') // Bottom note priority
    commandBottomNote(channel);
  else { // Last note priority  
    if (notes[channel][note]) {  // If note is on and using last note priority, add to ordered list
      orderIndx[channel] = (orderIndx[channel]+1) % 10;
      noteOrder[channel][orderIndx[channel]] = note;                 
    }
    commandLastNote(channel);
  }

      
}

void myNoteOff(byte channel, byte note, byte velocity) {
  Serial.print("Note Off, ch=");
  Serial.print(channel, DEC);
  Serial.print(", note=");
  Serial.print(note, DEC);
  Serial.print(", velocity=");
  Serial.println(velocity, DEC);
}

void myAfterTouchPoly(byte channel, byte note, byte velocity) {
  Serial.print("AfterTouch Change, ch=");
  Serial.print(channel, DEC);
  Serial.print(", note=");
  Serial.print(note, DEC);
  Serial.print(", velocity=");
  Serial.println(velocity, DEC);
}

void myControlChange(byte channel, byte control, byte value) {
  Serial.print("Control Change, ch=");
  Serial.print(channel, DEC);
  Serial.print(", control=");
  Serial.print(control, DEC);
  Serial.print(", value=");
  Serial.println(value, DEC);

  // CC range from 0 to 4095 mV  Left shift d2 by 5 to scale from 0 to 4095, 
  // and choose gain = 2X
  setVoltage(CC_DAC, CC_AB, 1, value<<5);  // DAC2, channel 1, gain = 2X

}

void myProgramChange(byte channel, byte program) {
  Serial.print("Program Change, ch=");
  Serial.print(channel, DEC);
  Serial.print(", program=");
  Serial.println(program, DEC);
}

void myAfterTouchChannel(byte channel, byte pressure) {
  Serial.print("After Touch, ch=");
  Serial.print(channel, DEC);
  Serial.print(", pressure=");
  Serial.println(pressure, DEC);
}

void myPitchChange(byte channel, int pitch) {
  Serial.print("Pitch Change, ch=");
  Serial.print(channel, DEC);
  Serial.print(", pitch=");
  Serial.println(pitch, DEC);
  
  // Pitch bend output from 0 to 1023 mV.  Left shift d2 by 4 to scale from 0 to 2047.
  // With DAC gain = 1X, this will yield a range from 0 to 1023 mV.  Additional amplification
  // after DAC will rescale to -1 to +1V.
  
  setVoltage(PITCH_DAC, PITCH_AB, 0, pitch<<4);  // DAC2, channel 0, gain = 1X 
}


// This 3-input System Exclusive function is more complex, but allows you to
// process very large messages which do not fully fit within the usbMIDI's
// internal buffer.  Large messages are given to you in chunks, with the
// 3rd parameter to tell you which is the last chunk.  This function is
// a Teensy extension, not available in the Arduino MIDI library.
//
void mySystemExclusiveChunk(const byte *data, uint16_t length, bool last) {
  Serial.print("SysEx Message: ");
  printBytes(data, length);
  if (last) {
    Serial.println(" (end)");
  } else {
    Serial.println(" (to be continued)");
  }
}

// This simpler 2-input System Exclusive function can only receive messages
// up to the size of the internal buffer.  Larger messages are truncated, with
// no way to receive the data which did not fit in the buffer.  If both types
// of SysEx functions are set, the 3-input version will be called by usbMIDI.
//
void mySystemExclusive(byte *data, unsigned int length) {
  Serial.print("SysEx Message: ");
  printBytes(data, length);
  Serial.println();
}

void myTimeCodeQuarterFrame(byte data) {
  static char SMPTE[8]={'0','0','0','0','0','0','0','0'};
  static byte fps=0;
  byte index = data >> 4;
  byte number = data & 15;
  if (index == 7) {
    fps = (number >> 1) & 3;
    number = number & 1;
  }
  if (index < 8 || number < 10) {
    SMPTE[index] = number + '0';
    Serial.print("TimeCode: ");  // perhaps only print when index == 7
    Serial.print(SMPTE[7]);
    Serial.print(SMPTE[6]);
    Serial.print(':');
    Serial.print(SMPTE[5]);
    Serial.print(SMPTE[4]);
    Serial.print(':');
    Serial.print(SMPTE[3]);
    Serial.print(SMPTE[2]);
    Serial.print('.');
    Serial.print(SMPTE[1]);  // perhaps add 2 to compensate for MIDI latency?
    Serial.print(SMPTE[0]);
    switch (fps) {
      case 0: Serial.println(" 24 fps"); break;
      case 1: Serial.println(" 25 fps"); break;
      case 2: Serial.println(" 29.97 fps"); break;
      case 3: Serial.println(" 30 fps"); break;
    }
  } else {
    Serial.print("TimeCode: invalid data = ");
    Serial.println(data, HEX);
  }
}

void mySongPosition(uint16_t beats) {
  Serial.print("Song Position, beat=");
  Serial.println(beats);
}

void mySongSelect(byte songNumber) {
  Serial.print("Song Select, song=");
  Serial.println(songNumber, DEC);
}

void myTuneRequest() {
  Serial.println("Tune Request");
}

void myClock() {
  Serial.println("Clock");
}

void myStart() {
  Serial.println("Start");
}

void myContinue() {
  Serial.println("Continue");
}

void myStop() {
  Serial.println("Stop");
}

void myActiveSensing() {
  Serial.println("Actvice Sensing");
}

void mySystemReset() {
  Serial.println("System Reset");
}

void myRealTimeSystem(uint8_t realtimebyte) {
  Serial.print("Real Time Message, code=");
  Serial.println(realtimebyte, HEX);
}



void printBytes(const byte *data, unsigned int size) {
  while (size > 0) {
    byte b = *data++;
    if (b < 16) Serial.print('0');
    Serial.print(b, HEX);
    if (size > 1) Serial.print(' ');
    size = size - 1;
  }
}

