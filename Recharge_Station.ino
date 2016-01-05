#define BAUD_RATE 57600

char versionStr[] = "Recharge_station which allows up to 29.0V down to 10V for 10 USB ports branch:whatwatt";

#include <Adafruit_NeoPixel.h>

#define VOLTLEDSTRIPPIN 13 // what pin the data input to the voltage LED strip is connected to
#define NUM_VOLTLEDS 22 // how many LEDs on the strip
Adafruit_NeoPixel voltLedStrip = Adafruit_NeoPixel(NUM_VOLTLEDS, VOLTLEDSTRIPPIN, NEO_GRB + NEO_KHZ800);

#define WHATWATTPIN 12 // what pin the WhatWatt handlebar pedalometer is connected to
#define NUM_POWER_PIXELS 7  // number LEDs for power
#define NUM_ENERGY_PIXELS 7  // number LEDs for energy
#define NUM_WHATWATTPIXELS (NUM_POWER_PIXELS+NUM_ENERGY_PIXELS)  // number LEDs per bike
Adafruit_NeoPixel whatWattStrip = Adafruit_NeoPixel(NUM_WHATWATTPIXELS, WHATWATTPIN, NEO_GRB + NEO_KHZ800);

#define ledBrightness 127 // brightness of addressible LEDs (0 to 255)

#define VOLTS_CUTOUT 10 // disconnect from the ultracaps below this voltage
#define VOLTS_CUTIN 12 // engage ultracap relay above this voltage
#define DISCORELAY 2 // relay cutoff output pin // NEVER USE 13 FOR A RELAY
#define CAPSRELAY 3 // relay override inhibitor transistor
#define VOLTPIN A0 // Voltage Sensor Pin
#define AMPSPIN A3 // Current Sensor Pin

// levels at which each LED turns green (normally all red unless below first voltage)
const float ledLevels[NUM_VOLTLEDS+1] = {
  10.2, 10.6, 11.05, 11.5, 12, 12.5, 13, 13.5, 14, 14.5, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27 };

#define AVG_CYCLES 50 // average measured values over this many samples
#define DISPLAY_INTERVAL 2000 // when auto-display is on, display every this many milli-seconds
#define BLINK_PERIOD 600
#define FAST_BLINK_PERIOD 150

#define STATE_OFF 0
#define STATE_BLINK 1
#define STATE_BLINKFAST 3
#define STATE_ON 2

#define MAX_VOLTS 29  // when to open the safety relay
#define RECOVERY_VOLTS 25  // when to close the safety relay
int relayState = STATE_OFF;

#define DANGER_VOLTS 30.0  // when to fast-flash white (slow-flash above last ledLevels)
int dangerState = STATE_OFF;

int blinkState = 0;
int fastBlinkState = 0;
int lastLedLevel = 0; // for LED strip hysteresis
int nowLedLevel = 0; // for LED strip
#define LEDLEVELHYSTERESIS 0.6 // how many volts of hysteresis for gas gauge

#define VOLTCOEFF 13.179  // larger number interprets as lower voltage
#define AMPCOEFF 8.0682 // 583 - 512 = 71; 71 / 8.8 amps = 8.0682
#define AMPOFFSET 512.0 // when current sensor is at 0 amps this is the ADC value

int voltsAdc = 0;
float voltsAdcAvg = 0;
float volts = 0;

//Current related variables
int ampsRaw = 0;
float amps = 0;

float watts = 0;
float energy = 0; // watt secs

// timing variables for various processes: led updates, print, blink, etc
unsigned long time = 0;
unsigned long timeFastBlink = 0;
unsigned long timeBlink = 0;
unsigned long timeDisplay = 0;

// var for looping through arrays
int i = 0;

uint32_t red; // needs to be initialized with .Color() in setup()
uint32_t green; // needs to be initialized with .Color() in setup()
uint32_t blue; // needs to be initialized with .Color() in setup()
uint32_t white; // needs to be initialized with .Color() in setup()
uint32_t dark; // needs to be initialized with .Color() in setup()

void setup() {
  Serial.begin(BAUD_RATE);

  Serial.println(versionStr);

  pinMode(DISCORELAY, OUTPUT);
  pinMode(CAPSRELAY,OUTPUT);

  voltLedStrip.begin(); // initialize the addressible LEDs
  voltLedStrip.show(); // clear their state
  whatWattStrip.begin(); // initialize the addressible LEDs
  whatWattStrip.show(); // clear their state

  red = voltLedStrip.Color(ledBrightness,0,0); // load these handy Colors
  green = voltLedStrip.Color(0,ledBrightness,0);
  blue = voltLedStrip.Color(0,0,ledBrightness);
  white = voltLedStrip.Color(ledBrightness,ledBrightness,ledBrightness);
  dark = voltLedStrip.Color(0,0,0);

  timeDisplay = millis();
  printDisplay();
}

void loop() {
  time = millis();
  getVolts();
  doSafety();

  doBlink();  // blink the LEDs
  doLeds();

  if(time - timeDisplay > DISPLAY_INTERVAL){
    printDisplay();
    timeDisplay = time;
  }

}

void doSafety() {
  if (volts > VOLTS_CUTIN) {
    digitalWrite(CAPSRELAY,HIGH);
  } else if (volts < VOLTS_CUTOUT) {
    digitalWrite(CAPSRELAY,LOW); // let the cap stay charged
    // nothing happens here because we shut off our own power
  }

  if (volts > MAX_VOLTS){
    digitalWrite(DISCORELAY, HIGH);
    relayState = STATE_ON;
  }

  if (relayState == STATE_ON && volts < RECOVERY_VOLTS){
    digitalWrite(DISCORELAY, LOW);
    relayState = STATE_OFF;
  }

  if (volts > DANGER_VOLTS){
    dangerState = STATE_ON;
  } 
  else {
    dangerState = STATE_OFF;
  }
}

void doBlink(){

  if (((time - timeBlink) > BLINK_PERIOD) && blinkState == 1){
    blinkState = 0;
    timeBlink = time;
  } 
  else if (((time - timeBlink) > BLINK_PERIOD) && blinkState == 0){
    blinkState = 1;
    timeBlink = time;
  }


  if (((time - timeFastBlink) > FAST_BLINK_PERIOD) && fastBlinkState == 1){
    fastBlinkState = 0;
    timeFastBlink = time;
  } 
  else if (((time - timeFastBlink) > FAST_BLINK_PERIOD) && fastBlinkState == 0){
    fastBlinkState = 1;
    timeFastBlink = time;
  }

}

void doLeds(){

  nowLedLevel = 0; // init value for this round
  for(i = 0; i < NUM_VOLTLEDS; i++) { // go through all but the last voltage in ledLevels[]
    if (volts < ledLevels[0]) { // if voltage below minimum
      voltLedStrip.setPixelColor(i,dark);  // all lights out
    } else if (volts > ledLevels[NUM_VOLTLEDS]) { // if voltage beyond highest level
      if (blinkState) { // make the lights blink
        voltLedStrip.setPixelColor(i,white);  // blinking white
      } else {
        voltLedStrip.setPixelColor(i,dark);  // blinking dark
      }
    } else { // voltage somewhere in between
      voltLedStrip.setPixelColor(i,dark);  // otherwise dark
      if (volts > ledLevels[i]) { // but if enough voltage
        nowLedLevel = i+1; // store what level we light up to
      }
    }
  }

  if (nowLedLevel > 0) { // gas gauge in effect
    if ((volts + LEDLEVELHYSTERESIS > ledLevels[nowLedLevel]) && (lastLedLevel == nowLedLevel+1)) {
        nowLedLevel = lastLedLevel;
      } else {
        lastLedLevel = nowLedLevel;
      }
    for(i = 0; i < nowLedLevel; i++) {
      voltLedStrip.setPixelColor(i,gasGaugeColor(i)); // gas gauge effect
    }
  } else {
  lastLedLevel = 0; // don't confuse the hysteresis
  }

  if (dangerState){ // in danger fastblink white
    for(i = 0; i < NUM_VOLTLEDS; i++) {
      if (fastBlinkState) { // make the lights blink FAST
        voltLedStrip.setPixelColor(i,white);  // blinking white
      } else {
        voltLedStrip.setPixelColor(i,dark);  // blinking dark
      }
    }
  }

  voltLedStrip.show(); // actually update the LED strip
} // END doLeds()

uint32_t gasGaugeColor(int ledNum) {
  if (ledNum < 5) {
    return red;
  } else if (ledNum < 20) {
    return blue;
  } else return white;
}

void getVolts(){
  voltsAdc = analogRead(VOLTPIN);
  voltsAdcAvg = average(voltsAdc, voltsAdcAvg);
  volts = adc2volts(voltsAdcAvg);
}

float average(float val, float avg){
  if(avg == 0)
    avg = val;
  return (val + (avg * (AVG_CYCLES - 1))) / AVG_CYCLES;
}

float adc2volts(float adc){
  return adc * (1 / VOLTCOEFF);
}

float adc2amps(int adc){
  return - (adc - AMPOFFSET) / AMPCOEFF;
}

void printDisplay(){
  Serial.print(volts);
  Serial.print("v (");
  Serial.print(analogRead(VOLTPIN));
  //  Serial.print(", a: ");
  //  Serial.print(amps);
  //  Serial.print(", va: ");
  //  Serial.print(watts);
  Serial.print(") nowLedLevel: ");
  Serial.print(nowLedLevel);
  Serial.print("  lastLedLevel: ");
  Serial.println(lastLedLevel);
}
