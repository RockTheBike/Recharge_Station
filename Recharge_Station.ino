#define BAUD_RATE 57600

char versionStr[] = "Recharge_station which allows up to 29.0V down to 10V for 10 USB ports with an extra pin for 20W Auxiliary LED for strong pedalers. branch:whatwatt";

#include <Adafruit_NeoPixel.h>

#define VOLTLEDSTRIPPIN 13 // what pin the data input to the voltage LED strip is connected to
#define NUM_VOLTLEDS 14 // how many LEDs on the strip
Adafruit_NeoPixel voltLedStrip = Adafruit_NeoPixel(NUM_VOLTLEDS, VOLTLEDSTRIPPIN, NEO_GRB + NEO_KHZ800);

#define POWERLEDPIN 12 // what pin the WhatWatt POWER pedalometer is connected to
#define ENERGYLEDPIN 11 // what pin the WhatWatt ENERGY pedalometer is connected to
#define NUM_POWER_PIXELS 17  // number LEDs for power
#define NUM_ENERGY_PIXELS 17  // number LEDs for energy
Adafruit_NeoPixel PowerStrip = Adafruit_NeoPixel(NUM_POWER_PIXELS, POWERLEDPIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel EnergyStrip = Adafruit_NeoPixel(NUM_ENERGY_PIXELS, ENERGYLEDPIN, NEO_GRB + NEO_KHZ800);

#define IND_BLINK_INTERVAL 300
#define IND_VOLT_LOW 17 // handlebar pedalometer blinks red
#define IND_VOLT_HIGH 34.0 // handlebar pedalometer blinks white
#define ledBrightness 127 // brightness of addressible LEDs (0 to 255)

#define AUXLED_ON_POINT 33 // could hook up a warning system of some kind
// volts_cut* and capsrelay are for dropstop system
#define VOLTS_CUTOUT 15 // disconnect from the ultracaps below this voltage
#define VOLTS_CUTIN 17 // engage ultracap relay above this voltage
#define DISCORELAY 2 // relay cutoff output pin // NEVER USE 13 FOR A RELAY
#define CAPSRELAY 3 // relay override inhibitor transistor
#define AUXLEDPIN 7 // relay override inhibitor transistor
#define VOLTPIN A0 // Voltage Sensor Pin
#define AMPSPIN A3 // Current Sensor Pin
#define NOISYZERO 0.5  // assume any smaller measurement should be 0

// levels at which each LED turns green (normally all red unless below first voltage)
const float ledLevels[NUM_VOLTLEDS+1] = {
  15, 16.4, 17.8, 19.2, 20.6, 22, 23.4, 24.8, 26.2, 27.6, 29, 30.4, 31.8, 33.2, 34.6 };

#define AVG_CYCLES 50 // average measured values over this many samples
#define OVERSAMPLING 25.0 // analog oversampling
#define DISPLAY_INTERVAL 250 // when auto-display is on, display every this many milli-seconds
#define ENERGY_INTERVAL 0
#define IND_INTERVAL 100
#define BUTTON_CHECK_INTERVAL 100
#define BLINK_PERIOD 600
#define FAST_BLINK_PERIOD 150

// scale the logarithmic displays:
// (we assume a relaxed pedaler produces 60W)
// barely turning the cranks produces 10W (not much more than noise)
#define MIN_POWER 10
// a sprinting athlete should just barely reach the top
#define MAX_POWER 1000

#ifndef WIMPIFY
// 15sec of relaxed pedaling should trigger minimally visible glow
#define MIN_ENERGY (float)(60*15)
// earn a smoothie with equivalent of a half-hour of relaxed pedaling
// ie 60W * 0.5hr * 3600s/hr
#define MAX_ENERGY (float)(60*0.5*3600)
#else
// re-tune for testers and other wimps:  earn a smoothie in seconds!
#define MIN_ENERGY (float)(60*5)
#define MAX_ENERGY (float)(60*10)
#endif

uint32_t ENERGY_COLORS[] = {
  Adafruit_NeoPixel::Color(0,0,0),
  Adafruit_NeoPixel::Color(255,0,0),
  Adafruit_NeoPixel::Color(0,255,0),
  Adafruit_NeoPixel::Color(255,0,255),
  Adafruit_NeoPixel::Color(255,128,0),
  Adafruit_NeoPixel::Color(0,255,255) };
#define NUM_ENERGY_COLORS (sizeof(ENERGY_COLORS)/sizeof(*ENERGY_COLORS))

#define STATE_OFF 0
#define STATE_ON 1
#define STATE_BLINK_LOW 2
#define STATE_BLINK_HIGH 3
#define STATE_RAMP 4

#define MAX_VOLTS 35.1 // 13 CAPS when to open the safety relay
#define RECOVERY_VOLTS 30  // when to close the safety relay
int relayState = STATE_OFF;

#define DANGER_VOLTS 36.0  // when to fast-flash white (slow-flash above last ledLevels)
int dangerState = STATE_OFF;

int blinkState = 0;
int fastBlinkState = 0;
int lastLedLevel = 0; // for LED strip hysteresis
int nowLedLevel = 0; // for LED strip
#define LEDLEVELHYSTERESIS 0.6 // how many volts of hysteresis for gas gauge

#define VOLTCOEFF 13.36  // larger number interprets as lower voltage
#define AMPCOEFF 13.05  // PLUSOUT = OUTPUT, PLUSRAIL = PEDAL INPUT
#define AMPOFFSET 118 // when current sensor is at 0 amps this is the ADC value

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
unsigned long lastEnergy = 0;
unsigned long lastButtonCheckTime = 0;
unsigned long lastIndicatorTime = 0;
int indState = STATE_RAMP;

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
  pinMode(AUXLEDPIN,OUTPUT);

  voltLedStrip.begin(); // initialize the addressible LEDs
  for (byte i = 0; i < voltLedStrip.numPixels(); i++) voltLedStrip.setPixelColor(i,1,1,1);
  voltLedStrip.show(); // clear their state
  EnergyStrip.begin(); // initialize the addressible LEDs
  for (byte i = 0; i < EnergyStrip.numPixels(); i++) EnergyStrip.setPixelColor(i,1,1,1);
  EnergyStrip.show(); // clear their state
  PowerStrip.begin(); // initialize the addressible LEDs
  for (byte i = 0; i < PowerStrip.numPixels(); i++) PowerStrip.setPixelColor(i,1,1,1);
  PowerStrip.show(); // clear their state

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

if(volts >= AUXLED_ON_POINT) {
  if (! digitalRead(AUXLEDPIN)) Serial.println("Turning on 100W of extra load for this strong pedaler.");
  digitalWrite(AUXLEDPIN,HIGH);
} else if (volts < (AUXLED_ON_POINT-1)) {
  if (digitalRead(AUXLEDPIN)) Serial.println("Turning off 100W of extra load because pedaler doesn't want it anymore.");
  digitalWrite((AUXLEDPIN),LOW);
}

  if(time - timeDisplay > DISPLAY_INTERVAL){
    printDisplay();
    timeDisplay = time;
  }

  if(time - lastEnergy > ENERGY_INTERVAL){
    doEnergy();
  }

  if(time - lastIndicatorTime > IND_INTERVAL){
    lastIndicatorTime = time;
    doIndicators();
  }

  if(time - lastButtonCheckTime > BUTTON_CHECK_INTERVAL){
    lastButtonCheckTime = time;
    doButtonCheck();
  }

}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  if (WheelPos < 85) {
    return Adafruit_NeoPixel::Color(255 - WheelPos * 3, WheelPos * 3, 0);
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    return Adafruit_NeoPixel::Color(0, 255 - WheelPos * 3, WheelPos * 3);
  } else {
    WheelPos -= 170;
    return Adafruit_NeoPixel::Color(WheelPos * 3, 0, 255 - WheelPos * 3);
  }
}

void doIndBlink(Adafruit_NeoPixel &strip){

  boolean indBlinkState = (time % (IND_BLINK_INTERVAL * 2) > IND_BLINK_INTERVAL);

  // turn all pixels off:
  if(!indBlinkState){
    for (byte i = 0; i < strip.numPixels(); i++) strip.setPixelColor(i,0,0,0);
  } else {
    if(indState == STATE_BLINK_LOW){
      for (byte i = 0; i < strip.numPixels(); i++) strip.setPixelColor(i,255,0,0);
    }else if(indState == STATE_BLINK_HIGH){
      for (byte i = 0; i < strip.numPixels(); i++) strip.setPixelColor(i,255,255,255);
    }
  }
}

void doIndRamp(){
  float ledstolight;

  // the power LEDs
  ledstolight = logPowerRamp(watts);
  if( ledstolight > NUM_POWER_PIXELS ) ledstolight=NUM_POWER_PIXELS;
  unsigned char hue = ledstolight/NUM_POWER_PIXELS * 170.0;
  uint32_t color = Wheel(hue<1?1:hue);
  static const uint32_t dark = Adafruit_NeoPixel::Color(0,0,0);
  doFractionalRamp(PowerStrip, 0, NUM_POWER_PIXELS, ledstolight, color, dark);

  // the energy LEDs
  int full_smoothies = energy/MAX_ENERGY;
  float partial_smoothie = energy - full_smoothies * MAX_ENERGY;
  ledstolight = logEnergyRamp(partial_smoothie);
  if( ledstolight > NUM_ENERGY_PIXELS ) ledstolight=NUM_ENERGY_PIXELS;
  uint32_t curcolor = ENERGY_COLORS[full_smoothies%NUM_ENERGY_COLORS];
  uint32_t nextcolor = ENERGY_COLORS[(full_smoothies+1)%NUM_ENERGY_COLORS];
  // doBackwardsFractionalRamp(PowerStrip, NUM_POWER_PIXELS, ledstolight, nextcolor, curcolor ); // for chained strips
  doFractionalRamp(EnergyStrip, 0, NUM_ENERGY_PIXELS, ledstolight, nextcolor, curcolor );

  // and show 'em
  PowerStrip.show();
  EnergyStrip.show();
}

// hacky utility to merge colors
// fraction=0 => colorA; fraction=1 => colorB; fraction=0.5 => mix
// TODO:  but something's backward in the code or my brain!
// (let's hope Adafruit_NeoPixel doesn't change its encoding of colors)
uint32_t weighted_average_of_colors( uint32_t colorA, uint32_t colorB,
  float fraction ){
  // TODO:  weight brightness to look more linear to the human eye
  uint8_t RA = (colorA>>16) & 0xff;
  uint8_t GA = (colorA>>8 ) & 0xff;
  uint8_t BA = (colorA>>0 ) & 0xff;
  uint8_t RB = (colorB>>16) & 0xff;
  uint8_t GB = (colorB>>8 ) & 0xff;
  uint8_t BB = (colorB>>0 ) & 0xff;
  return Adafruit_NeoPixel::Color(
    RA*fraction + RB*(1-fraction),
    GA*fraction + GB*(1-fraction),
    BA*fraction + BB*(1-fraction) );
}

void doFractionalRamp(Adafruit_NeoPixel &strip, uint8_t offset, uint8_t num_pixels, float ledstolight, uint32_t firstColor, uint32_t secondColor){
  for( int i=0,pixel=offset; i<=num_pixels; i++,pixel++ ){
    uint32_t color;
    if( i<(int)ledstolight )  // definitely firstColor
        color = firstColor;
    else if( i>(int)ledstolight )  // definitely secondColor
        color = secondColor;
    else  // mix the two proportionally
        color = weighted_average_of_colors( firstColor, secondColor, ledstolight-(int)ledstolight);
    strip.setPixelColor(pixel, color);
  }
}

// useful for the upside-down energy LEDs
void doBackwardsFractionalRamp(Adafruit_NeoPixel &strip, uint8_t offset, uint8_t num_pixels, float ledstolight, uint32_t firstColor, uint32_t secondColor){
  doFractionalRamp(strip, offset, num_pixels, num_pixels-ledstolight, secondColor, firstColor);
}

// Yay, a closed form solution, and it's even got meaningful parameters!

float logPowerRamp( float p ) {
  float l = log(p/MIN_POWER)*NUM_POWER_PIXELS/log(MAX_POWER/MIN_POWER);
  return l<0 ? 0 : l;
}

float logEnergyRamp( float e ) {
  float l = log(e/MIN_ENERGY)*NUM_ENERGY_PIXELS/log(MAX_ENERGY/MIN_ENERGY);
  return l<0 ? 0 : l;
}

void doIndicators(){

  if(volts < IND_VOLT_LOW){
    indState = STATE_BLINK_LOW;
  } else if(volts < IND_VOLT_HIGH){
    indState = STATE_RAMP;
  } else if (volts >= IND_VOLT_HIGH){
    indState = STATE_BLINK_HIGH;
  }

  if(indState == STATE_RAMP){
    doIndRamp();
  } else {
    doIndBlink(PowerStrip);
    doIndBlink(EnergyStrip);
  }
}

void doButtonCheck() {
  pinMode( ENERGYLEDPIN, INPUT_PULLUP );
  delay(10); // Need some time between pinMode and digitalRead for stuff to settle.
  if( ! digitalRead(ENERGYLEDPIN) ) { // button closes data line to ground
    energy = 0; // reset energy
    Serial.println("resetEnergy");
  }
  EnergyStrip.begin();
}

void doEnergy(){
  float timeDiff = time - lastEnergy;
  float timeDiffSecs = timeDiff / 1000.0;

  // measure amps and calc energy
  ampsRaw = 0; // reset adder
  for(int j = 0; j < OVERSAMPLING; j++) ampsRaw += analogRead(AMPSPIN) - AMPOFFSET;
  amps = ((float)ampsRaw / OVERSAMPLING) / AMPCOEFF; // PLUSOUT = OUTPUT, PLUSRAIL = PEDAL INPUT
  // we assume anything near or below zero is a reading error
  if( amps < NOISYZERO ) amps = 0;

  //calc watts and energy
  watts = volts * amps;
  float wattsecs = watts * timeDiffSecs;
  energy += wattsecs; // watt secs
  lastEnergy = time;
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
        voltLedStrip.setPixelColor(i,red);  // blinking dark
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
    for(i = 0; i < nowLedLevel; i++) { // gas gauge effect
      if (nowLedLevel < 5) {
        voltLedStrip.setPixelColor(i,red);
      } else {
        voltLedStrip.setPixelColor(i,green);
      }
      if (i >= 20) voltLedStrip.setPixelColor(i,white); // override with white for LEDs 20 and above
    }
  } else {
  lastLedLevel = 0; // don't confuse the hysteresis
  }

  if (dangerState){ // in danger fastblink white
    for(i = 0; i < NUM_VOLTLEDS; i++) {
      if (fastBlinkState) { // make the lights blink FAST
        voltLedStrip.setPixelColor(i,white);  // blinking white
      } else {
        voltLedStrip.setPixelColor(i,red);  // blinking dark
      }
    }
  }

  voltLedStrip.show(); // actually update the LED strip
} // END doLeds()

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

void printDisplay(){
  Serial.print(volts);
  Serial.print("v (");
  Serial.print(analogRead(VOLTPIN));
  Serial.print("  ");
  Serial.print(amps);
  Serial.print("A (");
  Serial.print(ampsRaw/OVERSAMPLING);
  Serial.print(")  (");
  Serial.print(analogRead(AMPSPIN));
  Serial.print(")  ");
  Serial.print(watts);
  Serial.print("W  ");
  Serial.print(energy);
  Serial.print("Watt secs  ");
  Serial.print(energy/3600,2);
  Serial.println("Watt hours");
}
