#include <TimerOne.h>
#include "InputDebounce.h"

// ***************
// DEBUG MODE
// ***************
// Uncomment to enable debug output. Note that
// MIDI timing clock won't be generated then but
// only debug messages will appear in USB serial output.
//#define DEBUG
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
#define BUTTON_DEBOUNCE_DELAY 20   // Tap debounce delay in ms

const long MAXIMUM_TAP_INTERVAL = 60L * 1000  * 10 / MINIMUM_BPM; // ms

int _bpm = 1200;  // BPM in tenths of a BPM!!

long _arrTaps[NUM_TAPS]; // store tap points in ms
unsigned long _lastTapTime = 0; // if last tap was too long ago we reset the tracking
unsigned long _timesTapped = 0;

volatile int _blinkCount = 0;

// Debounced tap pin
static InputDebounce tapPin;

void debugOutput(String output, bool appendNewline = true)
{
#ifdef DEBUG
  if (appendNewline)
    Serial.println(output);
  else
    Serial.print(output);
#endif
}

//================================================
// FUNCTIONS
//================================================

/*
 * Updates the midi clock timer by 'bpm'-variable
 * and stores the new value into EEPROM.
 */
void updateBpm()
{
  // Update the timer
  Timer1.setPeriod(calculateIntervalMicroSecs(_bpm));

#ifdef EEPROM_ADDRESS
  // Save the BPM in 2 bytes, MSB LSB
  EEPROM.write(EEPROM_ADDRESS, _bpm / 256);
  EEPROM.write(EEPROM_ADDRESS + 1, _bpm % 256);
#endif

  debugOutput("Set BPM to: ", false);
  debugOutput(String(_bpm / 10), false);
  debugOutput(".", false);
  debugOutput(String(_bpm % 10));
}

long calculateIntervalMicroSecs(int bpm)
{
  // Take care about overflows!
  return 60L * 1000 * 1000 * 10 / bpm / CLOCKS_PER_BEAT;
}

void resetTaps()
{
  _timesTapped = 0;
  for (int i = 0; i < NUM_TAPS; i++)
  {
    _arrTaps[i] = 0;
  }
}

//================================================
// MIDI CLOCK INTERRUPT
//================================================
/*
 * This function will be called by Timer lib as an interrupt.
 * It sends the clock pulse to midi and updates the LED blinking.
 */
void sendClockPulse() {
    // Write the timing clock byte
  #if !defined(DEBUG)
    Serial.write(MIDI_TIMING_CLOCK); 
  #endif
  _blinkCount = (_blinkCount + 1) % CLOCKS_PER_BEAT;
  if (_blinkCount == 0) {
    // Turn led on
    analogWrite(BLINK_OUTPUT_PIN, 255 - BLINK_PIN_POLARITY);
  } else if (_blinkCount == BLINK_TIME) {
    // Turn led off
    analogWrite(BLINK_OUTPUT_PIN, 0 + BLINK_PIN_POLARITY);
  }
}

//================================================
// TAP INPUT CALLBACK
//================================================
/*
 * Handles taps by setting millis and tap counter.
 * This function is called by 'InputDebounce' library.
 */
void tapInput(uint8_t pinIn) {
  debugOutput("Tap!");
  unsigned long now = millis();

   // Store the tapped value into an array
  _arrTaps[_timesTapped] = now;
  _lastTapTime = now;

  // Note : counter is updated in the end for 
  // compensating zero-based counting
  _timesTapped++;
}


//================================================
// SETUP
//================================================
void setup() {
  setupSerial();
  setupBlinking();
  setupEeprom();
  setupTapInput();
  debugOutput("Start");
}

void setupSerial() {
  // Set Serial boud-rate
  #ifdef DEBUG
    Serial.begin(DEBUG_BAUDRATE);
  #else
    Serial.begin(MIDI_BAUDRATE);
  #endif
}

void setupBlinking() {
  // Set pin modes
  pinMode(BLINK_OUTPUT_PIN, OUTPUT);
}

void setupEeprom() {
#ifdef EEPROM_ADDRESS
  // Get the saved BPM value from 2 stored bytes: MSB LSB
  bpm = EEPROM.read(EEPROM_ADDRESS) << 8;
  bpm += EEPROM.read(EEPROM_ADDRESS + 1);
  if (bpm < MINIMUM_BPM || bpm > MAXIMUM_BPM) {
    bpm = 1200;
  }
#endif
}

void setupTapInput() {
  // Setup debounced tapPin callback
  tapPin.registerCallbacks(tapInput, /*releasedCallback*/ NULL, /*pressedDurationCallback*/ NULL, /*releasedDurationCallback*/NULL);
  tapPin.setup(TAP_PIN, BUTTON_DEBOUNCE_DELAY, InputDebounce::PIM_INT_PULL_UP_RES);

  // Attach the interrupt to send the MIDI clock and start the timer
  Timer1.initialize();
  Timer1.setPeriod(calculateIntervalMicroSecs(_bpm));
  Timer1.attachInterrupt(sendClockPulse);
}

//================================================
// LOOP
//================================================
void loop() {
  unsigned long now = millis();
  
  // process debounced pin input
  tapPin.process(now);
  
  if (_timesTapped == 0)
    return;

  // Set bpm in loop if enough taps are available
  if (_timesTapped >= NUM_TAPS) {  
    for (int i=0; i<NUM_TAPS; i++){
      debugOutput(String(_arrTaps[i]));  
    }

    long avgTapInterval = (_arrTaps[NUM_TAPS - 1] - _arrTaps[0]) / (NUM_TAPS - 1); 
    debugOutput(String(avgTapInterval));
   
    _bpm = 60L * 1000 * 10 / avgTapInterval;

    updateBpm();

    // We set the bpm, so reset all values here
    resetTaps();
  }
  else if (_timesTapped < NUM_TAPS && (now - _arrTaps[_timesTapped - 1]) > MAXIMUM_TAP_INTERVAL) {
    // The tapping took too long so reset values here
    resetTaps();
    debugOutput("resetTaps : Loop");
  } 
}
