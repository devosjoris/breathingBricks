#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <esp_sleep.h>
//#include <WiFi.h>
#include <ThingSpeak.h>
#include <WiFiManager.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"


#define PUMP_ON_TIME 30000 //time in ms
#define SLEEP_TIME 86400000000ULL //24*3600*1E6 don't write it as a formula since that will cast it to long, it truncates to 500654080 which corresponds to 500s (9.5minutes)

/*#define PUMP_ON_TIME 5000 //time in ms
#define SLEEP_TIME 5*60* 1000000 //time in us (5 minutes)*/


//EPAPER

#define USE_EPAPER 1
#include <SPI.h>
#include "epd2in7_V2.h"
//#include "epd1in54_V2.h" the black and white
//#include "epd1in54B_V2.h" the black and white and red variant 
#include "imagedata.h"
#include "epdpaint.h"

#define COLORED     0
#define UNCOLORED   1

// Define the e-ink display and the SPI pins
//not changed compared to rev1
#define RST_PIN 16 //also update in epdif.h
#define DC_PIN 17 //also update in epdif.h
#define CS_PIN 10 //also update in epdif.h
#define BUSY_PIN 15 //also update in epdif.h

#define CLK_PIN 12 //also update in epdif.h
#define MOSI_PIN 11 //also update in epdif.h
#define LEDPIN 47


//if you have   another microcontroller or another e-ink display module you have to change the following   line
//GxEPD2_290_T94_V2
//GxEPD2_BW<GxEPD2_270, GxEPD2_270::HEIGHT> displayE(GxEPD2_270(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN));
SPIClass hspi(HSPI);

// Create a custom SPI instance
///SPIClass spiCustom(VSPI);

bool shouldEndProgram = false;

//Connect Wifi and to ThingSpeak

WiFiManager wifiManager;
const char* server = "api.thingspeak.com";
const char* writeAPIKey1 = "UPZ23G51STVNGGJ6";
const char* writeAPIKey2 = "HU4OTXS60L17SS6X";

//Connect to WebClient
WiFiClient client;
unsigned long Channel1 = 2413633; // Change to your channel number
unsigned long Channel2 = 2413649; // Change to your channel number
int myFieldNumber = 1; // Change to the field number you wish to update

// Pin definitions

const int pumpPin = 21;
const int moisturePin = 6;
const int FLOAT_SWITCH_PIN = -1; //no longer used
const int FLOAT_SWITCH_PIN2 = 5;
const int BUTTON_PIN =38;
const int V3V3Pin =3;
const int VBAT_DIV = 48;
const int LED_PIN = 47;
const int CHARGING_PIN = 14;

RTC_DATA_ATTR int bootCount = 0; //sleep wakeup counter


const int  CSPin =10;//use for adc?

// Moisture sensor variables
int moistureValue = 0;
int VBAT_DIV_Value = 0;
float defaultMoisture = 2500.0;

bool OLED_STATUS=false;
int EPAPER_STATUS=0;

#define USE_OLED 0

String WaterLevel = "";
// OLED display variables
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//lidar level sensor:
#define USE_LIDAR  1

uint32_t water_level = 0;
#define low_THRESHOLD 200 //incm
#define medium_THRESHOLD 100 //incm

/*#include "Adafruit_VL53L0X.h"
#Adafruit_VL53L0X lox = Adafruit_VL53L0X();
#define VL53 1
#define VL61 0

*/

#include "Adafruit_VL6180X.h"
Adafruit_VL6180X lox = Adafruit_VL6180X();
#define VL53 0
#define VL61 1

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

  Epd epd;


void printOLED(String toPrint1,String toPrint2,uint8_t txtSize){
  if(USE_OLED){
      if(!OLED_STATUS) { //TBD this has to run each time we enable the V3V pins (so on powerup!!)
      Serial.println(F("SSD1306 allocation failed"));
      }
      else{

        //Display Refill Water
      display.clearDisplay();
      display.setTextSize(txtSize);
      display.setTextColor(WHITE);
      display.setCursor(0, 0);
      display.setRotation(2);  // Rotate the display 180 degrees
      display.print(toPrint1);
      display.setCursor(0, 20);
      display.setRotation(2);  // Rotate the display 180 degrees
      display.print(toPrint2);
      display.display();
      delay(2000); // for debug
      }
  }
}


void printEP(String toPrint1,String toPrint2,String toPrint3){

  if(USE_EPAPER ==0) return;

  unsigned char image[1024];
  Paint paint(image, 176, 24);    //width should be the multiple of 8, this is a sub of the screen,,,
  for(int retry =0; retry<1; retry++){
    if(EPAPER_STATUS !=0) { //TBD this has to run each time we enable the V3V pins (so on powerup!!)
      Serial.println(F("SSD1306 allocation failed"));
    }
    else{

      //delay(1000);
      epd.Clear();
      //delay(1000);
      epd.Display_Base_color(0xff);
      //delay(1000);


      paint.Clear(COLORED);
      paint.DrawStringAt(0, 5, "Breathing Block", &Font16, UNCOLORED);
      epd.Display_Partial_Not_refresh(paint.GetImage(), 0, 32, 0+paint.GetWidth(), 32+paint.GetHeight());
      paint.Clear(UNCOLORED);
      paint.DrawStringAt(10, 5,toPrint1.c_str(), &Font16, COLORED);
      epd.Display_Partial_Not_refresh(paint.GetImage(), 0, 64, 0+paint.GetWidth(), 64+paint.GetHeight());
      
      paint.Clear(UNCOLORED);
      paint.DrawStringAt(10, 5, toPrint2.c_str(), &Font16, COLORED);
      epd.Display_Partial_Not_refresh(paint.GetImage(), 0, 96, 0+paint.GetWidth(), 96+paint.GetHeight());

      paint.Clear(UNCOLORED);
      paint.DrawStringAt(10, 5, toPrint3.c_str(), &Font16, COLORED);
      epd.Display_Partial_Not_refresh(paint.GetImage(), 0, 128, 0+paint.GetWidth(), 128+paint.GetHeight());

      epd.TurnOnDisplay();
    }
  }
  Serial.println("EPAPER_UPDATE_DONE");
  delay(10000);
}

void setup() {


  //print_wakeup_reason();
  
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  
  // Initialize serial communication
  Serial.begin(115200);

  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, LOW);

  // Initialize float switch pin
  pinMode(FLOAT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(FLOAT_SWITCH_PIN2, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLUP);



  Serial.println("ENABLE_V3V");
  pinMode(V3V3Pin, OUTPUT);
  digitalWrite(V3V3Pin, HIGH); //emable 3.3V switch LDO
  delay(100); //supply ramps

  Serial.println("ENABLE_v3vDONE");

if(USE_OLED){
    // Initialize OLED display
  Serial.println("ENABLE_OLED");
  OLED_STATUS=display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  printOLED("Hey There", "",2);
}

if(USE_LIDAR){
  if (!lox.begin()) {
    Serial.println(("Failed to boot VL53L0X"));
  }
 if(VL53) lox.startRangeContinuous();
}

//void init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration, bool pulldown_rst_mode, SPIClass& spi, SPISettings spi_settings)
  //SCK, MISO, MOSI, SS);
 // Begin custom SPI with the remapped MOSI pin


//tbd the code for the old bigger epaper we used is working so we are keeping it like this for the moment
//this is the arduino code in the waveshare repository (so not the esp32...)
  if(USE_EPAPER){
      EPAPER_STATUS = epd.Init();
      Serial.println("EPAPER_A");
      if(EPAPER_STATUS==0){
        Serial.println("EPAPER_B");
        epd.Clear();
        epd.Display_Base_color(0xff);
        Serial.println("EPAPER_C");
      }
  }
  Serial.println("EPAPER_DONE");
}

void loop() {


  /*const int pumpPin = 21;
  const int moisturePin = 6;
  const int FLOAT_SWITCH_PIN = 1;
  const int FLOAT_SWITCH_PIN2 = 2;
  const int V3V3Pin =14;
  const int VBAT_DIV = 7;*/
  //checking the DCDC PUMP / 3V3

  if(0){
      while(1) Serial.println(lox.readRange());

  }

  if(0){
    while(1){

      //for measuring VBAT_DIV need to enable the DCDC:
      Serial.println("ADC_CHECK");
      digitalWrite(pumpPin, HIGH);
      Serial.println("RD");
      delay(1000);
      
      pinMode(CSPin, INPUT);
      pinMode(moisturePin, INPUT);
      //adcAttachPin(moisturePin);
      /*moistureValue = analogRead(CSPin);
      moistureValue = analogRead(CSPin);
      moistureValue = analogRead(CSPin);
      moistureValue = analogRead(CSPin);
      moistureValue = analogRead(CSPin);
      moistureValue = analogRead(CSPin);*/
      moistureValue = analogRead(moisturePin);
      delay(1000);
      

      Serial.println("VBAT");
      Serial.println(VBAT_DIV_Value *3.3/4095.0);
      Serial.println("MOISTURE");
      Serial.println(moistureValue *3.3/4095.0);

    }

  }



    // Clear the display buffer
  if(USE_OLED) display.clearDisplay();
  Serial.println("CHECKING WATER");
  // Read moisture sensor
  moistureValue = analogRead(moisturePin);
  Serial.println("MOISTURE:");
  Serial.println(moistureValue);

  //high means level too low --> 
  printOLED("Checking", "Water",2);
  Serial.println("START_READOUT");
uint8_t switchValue =0;
  if(USE_LIDAR){
      if (1) {
        Serial.print("Distance in mm: ");
        water_level = lox.readRange();
        Serial.println(water_level);
        if(water_level > low_THRESHOLD)
          switchValue = 3;
        else if(water_level > medium_THRESHOLD)
          switchValue =0;
        else switchValue =1;
      }
  }

  if (USE_LIDAR ==0){ switchValue = digitalRead(FLOAT_SWITCH_PIN)  + 2*digitalRead(FLOAT_SWITCH_PIN2);}
  if (switchValue == 3) {//both switches are low
    Serial.println("Water Level NOT OK");
    printEP("Refill Water", "Can't water", "plants!"); 
    pinMode(LEDPIN, OUTPUT);
    digitalWrite(LEDPIN, HIGH); //emable 3.3V switch LDO


    //Display Refill Water
    /*display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.setRotation(2);  // Rotate the display 180 degrees
    display.print("Refill    Water");
    display.display();

    paint.Clear(COLORED);
    paint.DrawStringAt(20, 5, "Refill    Water", &Font16, UNCOLORED);
    epd.Display_Partial_Not_refresh(paint.GetImage(), 0, 96, 0+paint.GetWidth(), 96+paint.GetHeight());
    */

    //return;
    
  } else {
    Serial.println("Water Level Good");
    if (switchValue == 0){
      WaterLevel= "High.";
    }
    else{
      WaterLevel= "Medium.";
    }
    printEP("","Water Level", WaterLevel);
  }
 

  if (shouldEndProgram) {
    return; // End the program if the water level is not OK
  }
  
  // Display moisture level on OLED display
  /*
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.setRotation(2);  // Rotate the display 180 degrees
  display.print("Moisture ");
  display.setTextSize(3);
  display.setCursor(0, 20);
  display.setRotation(2);  // Rotate the display 180 degrees
  display.print(moistureValue);
  display.setTextSize(2);
  display.setCursor(75, 45);
  display.setRotation(2);  // Rotate the display 180 degrees
  display.print("   ");
  display.display();
  delay(2000); // Wait 2 seconds*/
  
  // If moisture level is above a threshold, turn on the pump
  printOLED("Checking", "Moisture",2);
  if (moistureValue > defaultMoisture) {
    //Make LEDLIGHT5 & 6 RED
    /*strip.setPixelColor(4, redColor); // Set light 5 to red
    strip.setPixelColor(5, redColor); // Set light 6 to red
    strip.show();*/
    //Turn PUMP ON
    if(switchValue ==3){ //water level is NOK
      printOLED("Can't water", "plants",1);
      printEP("Refill", "Immediately", "and press Reset.");
    }
    else{
      printOLED("Plant Dry", "Pumping",2);

      digitalWrite(pumpPin, HIGH);
      Serial.println("Pump turned on");

      //Make LEDLIGHT7 & 8 RED
      /*strip.setPixelColor(6, redColor); // Set light 7 to red
      strip.setPixelColor(7, redColor); // Set light 8 to red
      strip.show();*/
      delay(PUMP_ON_TIME); // Run Pump for 30 seconds

      //Turn Pump Off
      digitalWrite(pumpPin, LOW);
      Serial.println("Pump turned off");

      //Make Both Moisture (LIGHT5&6) and Pump (LIGHT7&8) Green
      /*strip.setPixelColor(4, greenColor); // Set light 5 to green
      strip.setPixelColor(5, greenColor); // Set light 6 to green
      strip.show();
      strip.setPixelColor(6, greenColor); // Set light 7 to green
      strip.setPixelColor(7, greenColor); // Set light 8 to green
      strip.show();*/
      printOLED("Plant OK","",2);
      printEP("Plant OK","Water Level", WaterLevel);
    }
  }
  else {
    Serial.println("No watering needed");
    if(switchValue !=3){
      printEP("Plant OK","Water Level", WaterLevel);
    }

  }


  //here we update the wifi:

  //reduce the current consumption by disabling everythign not wifi:
  //screen /moisture sensor and pump...
  //digitalWrite(V3V3Pin,LOW); probably ok to keep screen and moisture sensor on...
  digitalWrite(pumpPin,LOW);


  if(1){ //probably better to only enable the wifi when no pump /other activities to avoid stress on the battery / fuse/ burnout...
    // Initialize ThingSpeak
  ThingSpeak.begin(client);
  
  
  wifiManager.setConnectTimeout(60); // set the connect timeout to 20 seconds
  wifiManager.setTimeout(60); // set the portal timeout to 20 seconds
  unsigned long startTime = millis();
    while ((millis() - startTime) < 60000) {
      delay(1000); // Wait for 1 second before running the loop again
  // Make LED LIGHT1&2 red when not connected
      /*Serial.println("Making first 2 RED"); 
      strip.setPixelColor(0, redColor);
      strip.setPixelColor(1, redColor);
      Serial.println("First 2 are RED"); 
      delay(1000); 
      strip.show();
      delay(1000); 
*/
    if (wifiManager.autoConnect("BB-Connect")) {
        delay(1000); 
        Serial.println("WiFi connected");
        // Make LED LIGHT1&2 green when connected
       // Serial.println("Making first 2 GREEN"); 
        /*strip.setPixelColor(0, greenColor);
        strip.setPixelColor(1, greenColor);
        strip.show();
        delay(1000); */
        break; // exit the loop if connected successfully
    } else {
        Serial.println("");
        Serial.println("WiFi not connected but program will continue");
        // Keep LED LIGHT1&2 red when not connected 
        /*strip.setPixelColor(0, redColor);
        strip.setPixelColor(1, redColor);
        strip.show();
        */
        delay(1000); 
    }
  }
}



      // Write sensor value of moisture level to ThingSpeak
  ThingSpeak.setField(myFieldNumber, moistureValue);
  int status1 = ThingSpeak.writeFields(Channel1, writeAPIKey1);
  if (status1 == 200) {
    Serial.println("Channel update successful.");
  } else {
    Serial.println("Problem updating channel. HTTP error code " + String(status1));
  }

  // Write sensor value of water switch to ThingSpeak
  ThingSpeak.setField(myFieldNumber, switchValue);
  int status2 = ThingSpeak.writeFields(Channel2, writeAPIKey2);
  if (status2 == 200) {
    Serial.println("Channel update successful.");
  } else {
    Serial.println("Problem updating channel. HTTP error code " + String(status2));
  }




   // Make All Lights Green
    /*for (int i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, greenColor);
    }
    strip.show();*/
    Serial.println("SLEEPING");
    Serial.println(SLEEP_TIME);
    //esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, LOW); //this gives cpu reset so not ok..


    //Run again in 24 hours 
    //esp_sleep_enable_timer_wakeup(24*3600* 1000000); // Set wake-up time to 10 seconds
    //esp_sleep_enable_ext0_wakeup(GPIO_NUM_4,0);
    esp_sleep_enable_timer_wakeup(SLEEP_TIME); // Set wake-up time to 10 seconds
    esp_deep_sleep_start(); // Enter deep sleep mode
    //delay (24UL * 60UL * 60UL * 1000UL);
}
