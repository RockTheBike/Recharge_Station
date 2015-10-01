#define BAUD_RATE 57600

char versionStr[] = "Recharge_station which allows up to 29.0V down to 7V for 10 USB ports branch:easypedal";

#include <Adafruit_NeoPixel.h>
#define LEDSTRIPPIN 13 // what pin the data input to the LED strip is connected to
#define NUM_LEDS 22 // how many LEDs on the strip
Adafruit_NeoPixel ledStrip = Adafruit_NeoPixel(NUM_LEDS, LEDSTRIPPIN, NEO_GRB + NEO_KHZ800);
#define ledBrightness 127 // brightness of addressible LEDs (0 to 255)

// PINS
#define RELAYPIN 2 // relay cutoff output pin // NEVER USE 13 FOR A RELAY
#define RELAY_CONTROLPIN 4 // relay override inhibitor transistor
#define VOLTPIN A0 // Voltage Sensor Pin
#define UCAPVOLTPIN A1 // Voltage Sensor Pin for ultracap_minus
#define AMPSPIN A3 // Current Sensor Pin
#define BUCKPIN  9 // Transistors connecting ultracap_minus to ground

#define BUCK_MINIMUM 16 // minimum PWM value at all times
#define BUCK_BEGIN 10.0 // voltage at which to start charging ultracaps
#define BUCK_FULL  20.0 // voltage at which we enable full connection to ultracaps
#define BUCK_PWMJUMP 5  // PWM value difference worthy of updating analogWrite
int lastBuckPWM = 0; // remember the last analogWrite we made

// levels at which each LED turns green (normally all red unless below first voltage)
const float ledLevels[NUM_LEDS+1] = {
  8,  9, 10, 11.2, 12, 12.5, 13, 13.5, 14, 14.5, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27 };

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

int voltsAdc = 0;
float voltsAdcAvg = 0;
float volts = 0;
int ucapVoltsAdc = 0;
float ucapVoltsAdcAvg = 0;
float ucapVolts = 0;

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
  pinMode(BUCKPIN, OUTPUT);
  // setPwmFrequency(BUCKPIN,8); // 31250 / 8 = 3906.25 Hz (default is 64 = 488.28125 Hz)

  ledStrip.begin(); // initialize the addressible LEDs
  ledStrip.show(); // clear their state

  red = ledStrip.Color(ledBrightness,0,0); // load these handy Colors
  green = ledStrip.Color(0,ledBrightness,0);
  blue = ledStrip.Color(0,0,ledBrightness);
  white = ledStrip.Color(ledBrightness,ledBrightness,ledBrightness);
  dark = ledStrip.Color(0,0,0);

  timeDisplay = millis();
  printDisplay();
}

void loop() {
  time = millis();
  getVolts();
  doBuck();
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

void doBuck() {
  if (volts < BUCK_BEGIN) { // if pedaller isn't pedalling fast enough
    analogWrite(BUCKPIN,BUCK_MINIMUM);
    if (lastBuckPWM != BUCK_MINIMUM) {
      Serial.print("BUCK_MINIMUM");
      printDisplay();
    }
    lastBuckPWM = BUCK_MINIMUM;
  } else {
    byte targetBuckPWM = (byte)constrain( ((255.0 - BUCK_MINIMUM) * ( 1.0 - (float)(BUCK_FULL-volts) / (float)(BUCK_FULL-BUCK_BEGIN) ) ) + BUCK_MINIMUM , BUCK_MINIMUM, 255);
    if ((targetBuckPWM == 255) && (lastBuckPWM != targetBuckPWM)) {
      Serial.print("HIGH");
      printDisplay();
      digitalWrite(BUCKPIN,HIGH);
      lastBuckPWM = 255;
    }
    if (abs(targetBuckPWM - lastBuckPWM) > BUCK_PWMJUMP) { // if we need a different PWM value
      Serial.println(targetBuckPWM);
      analogWrite(BUCKPIN,targetBuckPWM);
      lastBuckPWM = targetBuckPWM; // we wrote this value
    }
  }
}

void doSafety() {
  if (volts > 5) {
    pinMode(RELAY_CONTROLPIN,OUTPUT); // we control relay now
    pinMode(RELAYPIN,OUTPUT); // we control relay now
    digitalWrite(RELAY_CONTROLPIN,HIGH); // we control relay now
    digitalWrite(RELAYPIN,LOW); // we control relay now
  } else {
    pinMode(RELAY_CONTROLPIN,INPUT); // voltage too low to control relay
    pinMode(RELAYPIN,INPUT);
  }

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

  nowLedLevel = 0; // init value for this round
  for(i = 0; i < NUM_LEDS; i++) { // go through all but the last voltage in ledLevels[]
    if (ucapVolts < ledLevels[0]) { // if voltage below minimum
      ledStrip.setPixelColor(i,dark);  // all lights out
    } else if (ucapVolts > ledLevels[NUM_LEDS]) { // if voltage beyond highest level
      if (blinkState) { // make the lights blink
        ledStrip.setPixelColor(i,white);  // blinking white
      } else {
        ledStrip.setPixelColor(i,dark);  // blinking dark
      }
    } else { // voltage somewhere in between
      ledStrip.setPixelColor(i,dark);  // otherwise dark
      if (ucapVolts > ledLevels[i]) { // but if enough voltage
        nowLedLevel = i+1; // store what level we light up to
      }
    }
  }

  if (nowLedLevel > 0) { // gas gauge in effect
    if ((ucapVolts + LEDLEVELHYSTERESIS > ledLevels[nowLedLevel]) && (lastLedLevel == nowLedLevel+1)) {
        nowLedLevel = lastLedLevel;
      } else {
        lastLedLevel = nowLedLevel;
      }
    for(i = 0; i < nowLedLevel; i++) {
      ledStrip.setPixelColor(i,gasGaugeColor(i)); // gas gauge effect
    }
  } else {
  lastLedLevel = 0; // don't confuse the hysteresis
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

uint32_t gasGaugeColor(int ledNum) {
  if (ledNum < 5) {
    return red;
  } else if (ledNum < 20) {
    return blue;
  } else return white;
}

void getAmps(){
  ampsAdc = analogRead(AMPSPIN);
  ampsAdcAvg = average(ampsAdc, ampsAdcAvg);
  amps = adc2amps(ampsAdcAvg);
}

void getVolts(){
  voltsAdc = analogRead(VOLTPIN);
  voltsAdcAvg = average(voltsAdc, voltsAdcAvg);
  volts = adc2volts(voltsAdcAvg);

  ucapVoltsAdc = analogRead(UCAPVOLTPIN);
  ucapVoltsAdcAvg = average(ucapVoltsAdc, ucapVoltsAdcAvg);
  ucapVolts = volts - adc2volts(ucapVoltsAdcAvg); // we measured the ultracap_minus voltage
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
  Serial.print(")  ucap:");
  Serial.print(ucapVolts);
  Serial.print("v (");
  Serial.print(analogRead(UCAPVOLTPIN));
  Serial.print(")  PWM:");
  Serial.print(lastBuckPWM);
  //  Serial.print(", a: ");
  //  Serial.print(amps);
  //  Serial.print(", va: ");
  //  Serial.print(watts);
  Serial.print("  nowLedLevel: ");
  Serial.print(nowLedLevel);
  Serial.print("  lastLedLevel: ");
  Serial.println(lastLedLevel);
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
