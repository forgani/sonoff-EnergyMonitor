 /*******************************************************************************
  ESP-12 based  
  This system helps to remotely monitor the current of the solar system by using a sonoff and a CT sensor.
  Mohammad Forgani, Forghanain
  https://www.forgani.com/electronics-projects/home-energy-monitor


  initial version 07 JUN 2021 
  Generic esp8266

  DOUT, 40, dtr, Disable, v2 Lower
  5A / 5mA 
******************************************************************************/

#define BLYNK_PRINT Serial
#include <BlynkSimpleEsp8266.h>
#include <WidgetRTC.h>
#include <math.h>
#include "EmonLib.h"                   // Include Emon Library
EnergyMonitor emon1;                   // Create an instance

/* Sonoff Basic
GPIO00 - BUTTON LOW 
GPIO12 - RELAY  	LED Rot und Relais  LOW = Ein
GPIO13 - LED1   	LED Blau 
GPIO03 - RX PIN
GPIO01 - TX PIN
GPIO14:  EXTRA 		GPIO
*/

/*
  Virtual Pins - Base
*/
#define vPIN_COST_VAL  V59
#define vPIN_VOLTAGE  V60


// L1
#define vPIN_CURRENT_L1_REAL V61
#define vPIN_CURRENT_L1_AVG V62
#define vPIN_CURRENT_L1_PEAK V63

// Test,  average of DAILY_AVG_CYCLE measures
#define vPIN_POWER_L1 V64
#define vPIN_CALIBRATION_L1 V65

// Test, without individual calibration parameters
#define vPIN_CURRENT_LF1_REAL V66

#define vPIN_BUTTON_RESET_AVG V67
#define vPIN_BUTTON_RESET_PEAK V68

#define vPIN_MONTOR_AVG_TIME V69
#define vPIN_MONTOR_PEAK_TIME V70

#define vPIN_DAILY_ENERGY_YESTERDAY_DATE V72 // Yesterday
#define vPIN_DAILY_ENERGY_USED V73
#define vPIN_DAILY_ENERGY_COST V76

#define vPIN_CURRENT_FULL_DATE_TIME V74
#define vPIN_CURRENT_DATE V75

#define ANALOG_INPUT A0

#define LED_PIN 13
#define BUTTON 0

float currentL1;
float currentL1_PEAK = 0.00;
float loadVoltage = 230;
float costFactor = 0.25;

long stopAvgWatch, stopPeakWatch;
char Day[8];
char Date[16];
char Time[8];
char TimeAndDate[32];
char FullDate[32];

BlynkTimer timer;
WidgetRTC rtc;

char auth[] = "xxx"; 
char ssid[] = "xxx"; 
char pass[] = "xxx"; 
IPAddress BlynkServerIP(xxx, xxx, xxx, xxx);
int port = xxx;

#define AVG_CYCLE 5
#define DAILY_AVG_CYCLE 5

float calibration = 15.00;

int avg_cycle = 0;
float currentL1_AVG[AVG_CYCLE + 1];
float currentL1_avg;

float powerL1 = 0;
int daily = false;


/*---------- SYNC ALL SETTINGS ON BOOT UP ----------*/
bool isFirstConnect = true;

BLYNK_CONNECTED() {
  if (isFirstConnect) {
    Blynk.syncAll();
    isFirstConnect = false;
  }
  // Synchronize time on connection
  rtc.begin();
}

void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.print("Connecting to ");  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("WiFi connected."); Serial.print("IP address:"); Serial.println(WiFi.localIP());

  timer.setInterval(8000L, CheckConnection); // check if still connected every 11s
  Blynk.config(auth, BlynkServerIP, port);
  Blynk.connect();
  Serial.println("Connected to Blynk server.");

  delay(10);

  // ZMCT103C Ratio/BurdenResistor. 1000/68=14.7    5.2A -> 68 ohm   75  -> 13.3
  // Burden Resistor (ohms) = (AREF * CT TURNS) / (2√2 * max primary current)
  emon1.current(ANALOG_INPUT, 13.3); // analog pin  1000/75 
  Blynk.virtualWrite(vPIN_CALIBRATION_L1, calibration);
  Blynk.virtualWrite(vPIN_VOLTAGE, loadVoltage);
  Blynk.virtualWrite(vPIN_COST_VAL, costFactor);

  // Display digital clock every 10 seconds
  timer.setInterval(5000L, stopAvgWatchCounter);
  timer.setInterval(5000L, stopPeakWatchCounter);
  timer.setInterval(1000L, getValues);
  timer.setInterval(1000L, clockDisplay);
  timer.setInterval(5000L, sendValuesMAX);
  pinMode(LED_PIN, OUTPUT);       // GPIO13 Blaue LED auf dem Sonoff
  pinMode(BUTTON, INPUT);        // define button as an input
}

void loop() {
  if (Blynk.connected())
    Blynk.run();
  timer.run();
}

static boolean ledStatus = false;

void getValues() {
  currentL1 = (emon1.calcIrms(1480) - calibration) / 2;
  Blynk.virtualWrite(vPIN_CURRENT_LF1_REAL, currentL1);
  if (currentL1 < 0) {
    currentL1 = 0.00;
  }
  Blynk.virtualWrite(vPIN_CURRENT_L1_REAL, currentL1);
  Serial.print( "currentL1 ="); Serial.println(currentL1);

  if (currentL1 > 0.3) {
    digitalWrite(LED_PIN, LOW);        //  LED einschalten
    delay(200);
  } else {
    digitalWrite(LED_PIN, HIGH);
  }
  sendValuesAVG();
}


// update the current average runtime values
void sendValuesAVG() {
  currentL1_AVG[avg_cycle] = currentL1;
  avg_cycle++;
  if (avg_cycle == AVG_CYCLE) {
    float L1_total = 0;
    for (int i = 0; i < (AVG_CYCLE - 1); i++) {
      L1_total += currentL1_AVG[i];
    }
    currentL1_avg = L1_total / AVG_CYCLE;
    currentL1_AVG[0] = currentL1_avg;
    avg_cycle = 1;
    Blynk.virtualWrite(vPIN_CURRENT_L1_AVG, currentL1_avg);
    Serial.print( "currentL1_avg ="); Serial.println(currentL1_avg);
  }
}

// set the MAX values
void sendValuesMAX() {
  if (currentL1_avg > currentL1_PEAK) {
    currentL1_PEAK = currentL1_avg;
    Blynk.virtualWrite(vPIN_CURRENT_L1_PEAK, currentL1_PEAK);
    Serial.print( "currentL1_PEAK ="); Serial.println(currentL1_PEAK);
  }
}

// execute every 10 second
void clockDisplay() {
  sprintf(Day, "%s,", dayShortStr(weekday()));
  sprintf(Date, "%02u %s %04d", day(), monthShortStr(month()), year());
  sprintf(Time, "%02d:%02d", hour(), minute());
  sprintf(TimeAndDate, "%s %s %s", Time, Day, Date);
  Blynk.virtualWrite(vPIN_CURRENT_FULL_DATE_TIME, TimeAndDate);
  powerL1 += currentL1*loadVoltage / 1000;
  Blynk.virtualWrite(vPIN_POWER_L1, powerL1);
  if (hour() == 23 && minute() == 59 && second() >= 58 && daily == false){ 
    sprintf(FullDate, "%s %s", Day, Date);
    Blynk.virtualWrite(vPIN_DAILY_ENERGY_YESTERDAY_DATE, FullDate);
	float dailyEnergy = powerL1 / 24;  //kWh
    if (dailyEnergy > 2) {
      float energyCostDaily = dailyEnergy * costFactor;
      Blynk.virtualWrite(vPIN_DAILY_ENERGY_USED, dailyEnergy);
      Blynk.virtualWrite(vPIN_DAILY_ENERGY_COST, energyCostDaily);
    }
    daily = true;
	  powerL1 = 0;
  } else if (hour() == 0 && minute() < 1){ 
	  daily = false;
  }
}


// stop averages timer
BLYNK_WRITE(vPIN_BUTTON_RESET_AVG) {
  if (param.asInt()) {
    Blynk.virtualWrite(vPIN_CURRENT_L1_AVG, 0);
    for (int i = 0; i < (AVG_CYCLE - 1); i++) {
      currentL1_AVG[i] = currentL1_avg;
    }
    avg_cycle = 0;
    Blynk.virtualWrite(vPIN_MONTOR_AVG_TIME, "00:00:00");
    stopAvgWatch = 0;
  }
}

// stop timer of the maximum value
BLYNK_WRITE(vPIN_BUTTON_RESET_PEAK) {
  if (param.asInt()) {
    Blynk.virtualWrite(vPIN_CURRENT_L1_PEAK, 0);
    currentL1_PEAK = 0;
    Blynk.virtualWrite(vPIN_MONTOR_PEAK_TIME, "00:00:00");
    stopPeakWatch = 0;
  }
}

// the stopwatch counter which is run on a timer
void stopAvgWatchCounter() {
  stopAvgWatch += 5;
  char WatchStr[16];
  int days = 0, hours = 0, mins = 0, secs = 0;
  secs = stopAvgWatch; //convect milliseconds to seconds
  mins = secs / 60; //convert seconds to minutes
  hours = mins / 60; //convert minutes to hours
  secs = secs - (mins * 60); //subtract the coverted seconds to minutes in order to display 59 secs max
  mins = mins - (hours * 60); //subtract the coverted minutes to hours in order to display 59 minutes max
  sprintf(WatchStr, "%02d:%02d:%02d", hours, mins, secs);
  Blynk.virtualWrite(vPIN_MONTOR_AVG_TIME, WatchStr);
  Serial.print( "WatchStr ="); Serial.println(WatchStr);
}

// the stopwatch counter which is run on a timer
void stopPeakWatchCounter() {
  stopPeakWatch += 5;
  char WatchStr[16];
  int days = 0, hours = 0, mins = 0, secs = 0;
  secs = stopPeakWatch;
  mins = secs / 60;
  hours = mins / 60;
  secs = secs - (mins * 60);
  mins = mins - (hours * 60);
  sprintf(WatchStr, "%02d:%02d:%02d", hours, mins, secs);
  Blynk.virtualWrite(vPIN_MONTOR_PEAK_TIME, WatchStr);
  Serial.print( "WatchStr ="); Serial.println(WatchStr);
}

void CheckConnection() { // check every 11s if connected to Blynk server
  if (!Blynk.connected()) {
    Serial.println("Not connected to Blynk server");
    Blynk.connect(); // try to connect to server with default timeout
  } else {
    Serial.println("Connected to Blynk server");
  }
}

BLYNK_WRITE(vPIN_CALIBRATION_L1) {// calibration slider 50 to 70  
  calibration = param.asFloat();
}

// SUPPLY VOLTAGE
BLYNK_WRITE(vPIN_VOLTAGE) { // set supply voltage slider 210 to 260
  loadVoltage = param.asFloat();
}

//COST FACTOR
BLYNK_WRITE(vPIN_COST_VAL) { // PF slider 60 to 100 i.e 0.60 to 1.00, default 95
  costFactor = param.asFloat();
}
