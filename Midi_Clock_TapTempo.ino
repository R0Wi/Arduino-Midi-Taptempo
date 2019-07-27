#include <TimerOne.h>

// ***************
// DEBUG MODE
// ***************
bool debug = false;
#define DEBUG_BAUDRATE 115200

/*
   FEATURE: TAP BPM INPUT
*/
#define TAP_PIN 3
#define TAP_PIN_POLARITY FALLING
#define NUM_TAPS 4

/*
   FEATURE: BLINK TEMPO LED
*/
#define BLINK_OUTPUT_PIN 13
#define BLINK_PIN_POLARITY 0  // 0 = POSITIVE, 255 - NEGATIVE
#define BLINK_TIME 4 // How long to keep LED lit in CLOCK counts (so range is [0,24])

/*
   FEATURE: EEPROM BPM storage
*/
//#define EEPROM_ADDRESS 0 // Where to save BPM
#ifdef EEPROM_ADDRESS
#include <EEPROM.h>
#endif

/*
   GENERAL PARAMETERS
*/
#define MIDI_TIMING_CLOCK 0xF8
#define CLOCKS_PER_BEAT 24
#define MINIMUM_BPM 400 // Used for debouncing
#define MAXIMUM_BPM 3000 // Used for debouncing
#define MIDI_BAUDRATE 31250 // Serial Midi connection 
//#define MIDI_BAUDRATE 115200

long intervalMicroSeconds;
int bpm = 1200;  // BPM in tenths of a BPM!!

long minimumTapInterval = 60L * 1000 * 1000 * 10 / MAXIMUM_BPM;
long maximumTapInterval = 60L * 1000 * 1000 * 10 / MINIMUM_BPM;
volatile long debounceTime = 350000;

volatile long arrTaps[NUM_TAPS];
volatile long lastTapTime = 0; // for debouncing

volatile long timesTapped = 0;
volatile int blinkCount = 0;

void sendClockPulse() {
    // Write the timing clock byte
  if (!debug)
    Serial.write(MIDI_TIMING_CLOCK); 

  blinkCount = (blinkCount + 1) % CLOCKS_PER_BEAT;
  if (blinkCount == 0) {
    // Turn led on
    analogWrite(BLINK_OUTPUT_PIN, 255 - BLINK_PIN_POLARITY);
  } else if (blinkCount == BLINK_TIME) {
    // Turn led off
    analogWrite(BLINK_OUTPUT_PIN, 0 + BLINK_PIN_POLARITY);
  }
}

void updateBpm() {
  // Update the timer
  long interval = calculateIntervalMicroSecs(bpm);
  Timer1.setPeriod(interval);
  
  #ifdef EEPROM_ADDRESS
    // Save the BPM in 2 bytes, MSB LSB
    EEPROM.write(EEPROM_ADDRESS, bpm / 256);
    EEPROM.write(EEPROM_ADDRESS + 1, bpm % 256);
  #endif

  if (debug){
    Serial.print("Set BPM to: ");
    Serial.print(bpm / 10);
    Serial.print('.');
    Serial.println(bpm % 10);
  }
}

long calculateIntervalMicroSecs(int bpm) {
  // Take care about overflows!
  return 60L * 1000 * 1000 * 10 / bpm / CLOCKS_PER_BEAT;
}

void resetTaps(){
  timesTapped = 0;
    for(int i = 0; i < NUM_TAPS; i++){
      arrTaps[i] = 0;
    }
}

void debugOutput(String output){
  if (debug){
    Serial.println(output);
  }
}

void tapInput() {  
  // ------------------------------------------
  //  HANDLE TAPS BY SETTING MICROS AND COUNTER
  // ------------------------------------------
  long now = micros();

  // Calculate difference between last tap and now for debouncing
  long diff = now - lastTapTime;
  if (diff < debounceTime) {
    return; // Debounce
  }

  debugOutput("Tap!");

  // Store the tapped value into an array
  arrTaps[timesTapped] = now;
  lastTapTime = now;

  // Note : counter is updated in the end for 
  // compensating zero-based counting
  timesTapped++;
}

//----------------------
//  SETUP
//----------------------
void setup() {
  // Set Serial boud-rate
  if (debug)
    Serial.begin(DEBUG_BAUDRATE);
  else
    Serial.begin(MIDI_BAUDRATE);
    
  debugOutput("Start");

  // Set pin modes
  pinMode(BLINK_OUTPUT_PIN, OUTPUT);

#ifdef EEPROM_ADDRESS
  // Get the saved BPM value from 2 stored bytes: MSB LSB
  bpm = EEPROM.read(EEPROM_ADDRESS) << 8;
  bpm += EEPROM.read(EEPROM_ADDRESS + 1);
  if (bpm < MINIMUM_BPM || bpm > MAXIMUM_BPM) {
    bpm = 1200;
  }
#endif

  pinMode(TAP_PIN, INPUT_PULLUP);
  // Interrupt for catching tap events
  attachInterrupt(digitalPinToInterrupt(TAP_PIN), tapInput, TAP_PIN_POLARITY);

  // Attach the interrupt to send the MIDI clock and start the timer
  Timer1.initialize(intervalMicroSeconds);
  Timer1.setPeriod(calculateIntervalMicroSecs(bpm));
  Timer1.attachInterrupt(sendClockPulse);
}

//-------------------
//  LOOP
//------------------
void loop() {
  if (timesTapped == 0)
    return;
    
  long now = micros();

  // Set bpm in loop if enough taps are available
  if (timesTapped >= NUM_TAPS) {  
    for (int i=0; i<NUM_TAPS; i++){
      debugOutput(String(arrTaps[i]));  
    }
    long avgTapInterval = (arrTaps[NUM_TAPS - 1] - arrTaps[0]) / (NUM_TAPS - 1); 
    debugOutput(String(avgTapInterval));
    bpm = 60L * 1000 * 1000 * 10 / avgTapInterval;
    // Update blinkCount to make sure LED blink matches tapped beat
    //blinkCount = ((now - arrTaps[NUM_TAPS - 2]) * 24 / avgTapInterval) % CLOCKS_PER_BEAT;  // TODO
    updateBpm();

    // We set the bpm, so reset all values here
    resetTaps();
  }
  else if (timesTapped < NUM_TAPS && (now - arrTaps[timesTapped - 1]) > maximumTapInterval) {
    // The tapping took too long so reset values here
    resetTaps();
    debugOutput("resetTaps : Loop");
  } 
}
