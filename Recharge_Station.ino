#define BAUD_RATE 57600
/**** Single-rail Pedalometer
 * Arduino code to run the Dance with Lance Arbduino
 * ver. 1.14
 * Written by:
 * Thomas Spellman <thomas@thosmos.com>
 * Jake <jake@spaz.org>
 * Paul@rockthebike.com
 *
 * Notes:
 * 1.6 - moved version to the top, started protocol of commenting every change in file and in Git commit
 * 1.7 - jake 6-21-2012 disable minusalert until minus rail is pedaled at least once (minusAlertEnable and startupMinusVoltage)
 * 1.8 -- FF added annoying light sequence for when relay fails or customer bypasses protection circuitry.+
 * 1.9 - TS => cleaned up a bit, added state constants, turn off lowest 2 levels when level 3 and above
 * 1.10 - TS => cleaned up levels / pins variables, changed to a "LEDs" naming scheme
 * 1.11 - TS => does a very slow 4digits watts average, fixed the high blink
 * 1.12 - TS => printWatts uses D4Avg instead of watts, 300 baud
 * 1.13 - TS => D4Avg fix, 2400 baud
 * 1.14 - FF => Added CalcWattHours function, changing the Sign's data to Watt Hours, instead of Watts, in time for BMF VII
 * 1.15 - JS => started adding buck converter stuff 
 * 2.1 - JS => changed to split_rail_48v_4level, adding PWM for LED pedalometer, turning off buck converter and sign output
 * 2.15 - JS => fixed so white LEDs are solid before starting to blink at 50v, tuned relay voltages
 * 2.2 - JS => create branch 1b1i for onebike-oneinverter which buck converts up to 60V down to 12V for inverter
 * 2.3 - JS => create branch decida for split-rail system with automatic rail selection for pedallers (see decida.xcf)
 * 2.4 - JS => rip out a bunch of stuff that we haven't used in a long time
 * 2.5 - JS => create branch 1b1l for onebike-onelaptop which buck converts up to 40.5V down to 19V for laptop/projector
 * 2.6 - JS => create branch 1b10usb for onebike-10usb which allows up to 27.0V down to 7V for 10 USB ports
*/
char versionStr[] = "Split-Rail 48 volt 4-line pedalometer Pedal Power Utility Box ver. 2.6 branch:1b10usb";

#include <Adafruit_NeoPixel.h>
#define LEDSTRIPPIN 13 // what pin the data input to the LED strip is connected to
#define NUM_LEDS 22 // how many LEDs on the strip
Adafruit_NeoPixel ledStrip = Adafruit_NeoPixel(NUM_LEDS, LEDSTRIPPIN, NEO_GRB + NEO_KHZ800);
#define ledBrightness 127 // brightness of addressible LEDs (0 to 255)

// PINS
#define RELAYPIN 2 // relay cutoff output pin // NEVER USE 13 FOR A RELAY
#define VOLTPIN A0 // Voltage Sensor Pin
#define AMPSPIN A3 // Current Sensor Pin

// levels at which each LED turns green (normally all red unless below first voltage)
const float ledLevels[NUM_LEDS+1] = {
  10,  9, 10, 11.2, 12, 12.5, 13, 13.5, 14, 14.5, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27 };
//red  1grn   2grn   3grn   4grn   5grn   6grn   white

#define AVG_CYCLES 50 // average measured values over this many samples
#define DISPLAY_INTERVAL 2000 // when auto-display is on, display every this many milli-seconds
#define LED_UPDATE_INTERVAL 1000
#define BLINK_PERIOD 600
#define FAST_BLINK_PERIOD 150

#define STATE_OFF 0
#define STATE_BLINK 1
#define STATE_BLINKFAST 3
#define STATE_ON 2

#define MAX_VOLTS 28  // when to open the safety relay
#define RECOVERY_VOLTS 25  // when to close the safety relay
int relayState = STATE_OFF;

#define DANGER_VOLTS 30.0  // when to fast-flash white (slow-flash above last ledLevels)
int dangerState = STATE_OFF;

int blinkState = 0;
int fastBlinkState = 0;

#define VOLTCOEFF 13.179  // larger number interprets as lower voltage

int voltsAdc = 0;
float voltsAdcAvg = 0;
float volts = 0;

int voltsBuckAdc = 0; // for measuring A1 voltage
float voltsBuckAvg = 0; // for measuring A1 voltage
float voltsBuck = 0; // averaged A1 voltage

//Current related variables
int ampsAdc = 0;
float ampsAdcAvg = 0;
float amps = 0;

float watts = 0;
float wattHours = 0;

// timing variables for various processes: led updates, print, blink, etc
unsigned long time = 0;
unsigned long timeFastBlink = 0;
unsigned long timeBlink = 0;
unsigned long timeDisplay = 0;
unsigned long wattHourTimer = 0;

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

  pinMode(RELAYPIN, OUTPUT);

  ledStrip.begin(); // initialize the addressible LEDs
  ledStrip.show(); // clear their state

  red = ledStrip.Color(ledBrightness,0,0); // load these handy Colors
  green = ledStrip.Color(0,ledBrightness,0);
  blue = ledStrip.Color(0,0,ledBrightness);
  white = ledStrip.Color(ledBrightness,ledBrightness,ledBrightness);
  dark = ledStrip.Color(0,0,0);

  timeDisplay = millis();
  // setPwmFrequency(3,1); // this sets the frequency of PWM on pins 3 and 11 to 31,250 Hz
  // setPwmFrequency(9,1); // this sets the frequency of PWM on pins 9 and 10 to 31,250 Hz
  // digitalWrite(9,LOW);
  // pinMode(9,OUTPUT); // this pin will control the transistors of the huge BUCK converter
}

void loop() {
  time = millis();
  getVolts();
  // doBuck(); // adjust inverter voltage
  doSafety();
  //  getAmps();  // only if we have a current sensor
  //  calcWatts(); // also adds in knob value for extra wattage, unless commented out

  //  if it's been at least 1/4 second since the last time we measured Watt Hours...
  /*  if (time - wattHourTimer >= 250) {
   calcWattHours();
   wattHourTimer = time; // reset the integrator    
   }
  */
  doBlink();  // blink the LEDs
  doLeds();

  if(time - timeDisplay > DISPLAY_INTERVAL){
    // printWatts();
    //    printWattHours();
    printDisplay();
    timeDisplay = time;
  }

}

#define BUCK_CUTIN 18 // voltage above which transistors can start working
#define BUCK_CUTOUT 17.5 // voltage below which transistors can not function
#define BUCK_VOLTAGE 18.1 // target voltage for laptop to be supplied with
#define BUCK_VOLTPIN A1 // this pin measures inverter's MINUS TERMINAL voltage
#define BUCK_HYSTERESIS 0.75 // volts above BUCK_VOLTAGE where we start regulatin
#define BUCK_PWM_UPJUMP 0.03 // amount to raise PWM value if voltage is below BUCK_VOLTAGE
#define BUCK_PWM_DOWNJUMP 0.15 // amount to lower PWM value if voltage is too high
float buckPWM = 0; // PWM value of pin 9
int lastBuckPWM = 0; // make sure we don't call analogWrite if already set right

void doBuck() {
  if (volts > BUCK_CUTIN) { // voltage is high enough to turn on transistors
    if (volts <= BUCK_VOLTAGE) { // system voltage is lower than inverter target voltage
      digitalWrite(9,HIGH); // turn transistors fully on, give full voltage to inverter
      buckPWM = 0;
    }

    if ((volts > BUCK_VOLTAGE+BUCK_HYSTERESIS) && (buckPWM == 0)) { // begin PWM action
      buckPWM = 220.0 * (1.0 - ((volts - BUCK_VOLTAGE) / BUCK_VOLTAGE)); // low best guess for initial PWM value
      //      Serial.print("buckval=");
      //      Serial.println(buckPWM);
      analogWrite(9,(int) buckPWM); // actually set the thing in motion
    }

    if ((volts > BUCK_VOLTAGE)) { // && (buckPWM != 0)) { // adjust PWM value based on results
      if (volts - voltsBuck > BUCK_VOLTAGE + BUCK_HYSTERESIS) { // inverter voltage is too high
        buckPWM -= BUCK_PWM_DOWNJUMP; // reduce PWM value to reduce inverter voltage
        if (buckPWM <= 0) {
          //          Serial.print("0");
          buckPWM = 0; // minimum PWM value
        }
        if (lastBuckPWM != (int) buckPWM) { // only if the PWM value has changed should we...
          lastBuckPWM = (int) buckPWM;
          //          Serial.print("-");
          analogWrite(9,lastBuckPWM); // actually set the PWM value
        }
      }
      if (volts - voltsBuck < BUCK_VOLTAGE) { // inverter voltage is too low
        buckPWM += BUCK_PWM_UPJUMP; // increase PWM value to raise inverter voltage
        if (buckPWM > 255.0) {
          buckPWM = 255.0;
          //          Serial.print("X");
        }
        if (lastBuckPWM != (int) buckPWM) { // only if the PWM value has changed should we...
          lastBuckPWM = (int) buckPWM;
          //          Serial.print("+");
          analogWrite(9,lastBuckPWM); // actually set the PWM value
        }
      }
    }
  } 
  if (volts < BUCK_CUTOUT) { // system voltage is too low for transistors
    digitalWrite(9,LOW); // turn off transistors
  }
}

void doSafety() {
  if (volts > MAX_VOLTS){
    digitalWrite(RELAYPIN, HIGH);
    relayState = STATE_ON;
  }

  if (relayState == STATE_ON && volts < RECOVERY_VOLTS){
    digitalWrite(RELAYPIN, LOW);
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

  for(i = 0; i < NUM_LEDS; i++) { // go through all voltages in ledLevels[]
    if (volts < ledLevels[0]) { // if voltage below minimum
      ledStrip.setPixelColor(i,dark);  // all lights out
    } else if (volts > ledLevels[NUM_LEDS-1]) { // if voltage beyond highest level
      if (blinkState) { // make the lights blink
        ledStrip.setPixelColor(i,white);  // blinking white
      } else {
        ledStrip.setPixelColor(i,dark);  // blinking dark
      }
    } else { // voltage somewhere in between
      ledStrip.setPixelColor(i,red);  // otherwise red
      if (volts > ledLevels[i+1]) { // but if enough voltage
        ledStrip.setPixelColor(i,blue);  // gas gauge effect
      }
    }
  }

  if (dangerState){ // in danger fastblink white
    for(i = 0; i < NUM_LEDS; i++) {
      if (fastBlinkState) { // make the lights blink FAST
        ledStrip.setPixelColor(i,white);  // blinking white
      } else {
        ledStrip.setPixelColor(i,dark);  // blinking dark
      }
    }
  }

  ledStrip.show(); // actually update the LED strip
} // END doLeds()

void getAmps(){
  ampsAdc = analogRead(AMPSPIN);
  ampsAdcAvg = average(ampsAdc, ampsAdcAvg);
  amps = adc2amps(ampsAdcAvg);
}

void getVolts(){
  voltsAdc = analogRead(VOLTPIN);
  voltsAdcAvg = average(voltsAdc, voltsAdcAvg);
  volts = adc2volts(voltsAdcAvg);

  voltsBuckAdc = analogRead(BUCK_VOLTPIN);
  voltsBuckAvg = average(voltsBuckAdc, voltsBuckAvg);
  voltsBuck = adc2volts(voltsBuckAvg);
}

float average(float val, float avg){
  if(avg == 0)
    avg = val;
  return (val + (avg * (AVG_CYCLES - 1))) / AVG_CYCLES;
}

float adc2volts(float adc){
  return adc * (1 / VOLTCOEFF);
}

float adc2amps(float adc){
  return (adc - 512) * 0.1220703125;
}

void calcWatts(){
  watts = volts * amps;
//  doKnob(); // only if we have a knob to look at
//  watts += knobAdc / 2; // uncomment this line too
}

void calcWattHours(){
  wattHours += (watts * ((time - wattHourTimer) / 1000.0) / 3600.0); // measure actual watt-hours
  //wattHours +=  watts *     actual timeslice / in seconds / seconds per hour
  // In the main loop, calcWattHours is being told to run every second.
}

void printWatts(){
  Serial.print("w");
  Serial.println(watts);
}

void printWattHours(){
  Serial.print("w"); // tell the sign to print the following number
  //  the sign will ignore printed decimal point and digits after it!
  Serial.println(wattHours,1); // print just the number of watt-hours
  //  Serial.println(wattHours*10,1); // for this you must put a decimal point onto the sign!
}

void printDisplay(){
  Serial.print(volts);
  Serial.print("v (");
  Serial.print(analogRead(VOLTPIN));
  //  Serial.print(", a: ");
  //  Serial.print(amps);
  //  Serial.print(", va: ");
  //  Serial.print(watts);
  Serial.print("), voltsBuck: ");
  Serial.print(voltsBuck);
  Serial.print("v, laptop: ");
  Serial.print(volts-voltsBuck);
  Serial.print("v, buckval=");
  Serial.println(buckPWM);

  //  Serial.print(", Levels ");
  //  for(i = 0; i < NUM_LEDS; i++) {
  //    Serial.print(i);
  //    Serial.print(": ");
  //    Serial.print(ledState[i]);
  //    Serial.print(", ");
  //  }
  //  Serial.println("");
  // Serial.println();
}

void setPwmFrequency(int pin, int divisor) {
  byte mode;
  if(pin == 5 || pin == 6 || pin == 9 || pin == 10) {
    switch(divisor) {
    case 1: 
      mode = 0x01; 
      break;
    case 8: 
      mode = 0x02; 
      break;
    case 64: 
      mode = 0x03; 
      break;
    case 256: 
      mode = 0x04; 
      break;
    case 1024: 
      mode = 0x05; 
      break;
    default: 
      return;
    }
    if(pin == 5 || pin == 6) {
      TCCR0B = TCCR0B & 0b11111000 | mode;
    } 
    else {
      TCCR1B = TCCR1B & 0b11111000 | mode;
    }
  } 
  else if(pin == 3 || pin == 11) {
    switch(divisor) {
    case 1: 
      mode = 0x01; 
      break;
    case 8: 
      mode = 0x02; 
      break;
    case 32: 
      mode = 0x03; 
      break;
    case 64: 
      mode = 0x04; 
      break;
    case 128: 
      mode = 0x05; 
      break;
    case 256: 
      mode = 0x06; 
      break;
    case 1024: 
      mode = 0x7; 
      break;
    default: 
      return;
    }
    TCCR2B = TCCR2B & 0b11111000 | mode;
  }
}
