/*
  Reading multiple RFID tags, simultaneously!
  By: Nathan Seidle @ SparkFun Electronics
  Date: October 3rd, 2016
  https://github.com/sparkfun/Simultaneous_RFID_Tag_Reader

  Constantly reads and outputs any tags heard

  If using the Simultaneous RFID Tag Reader (SRTR) shield, make sure the serial slide
  switch is in the 'SW-UART' position
*/
#include <Arduino.h>
#include <SoftwareSerial.h> //Used for transmitting to the device
#include <set>
#include <string>
std::set<String> productSet;
SoftwareSerial softSerial(26, 25); //RX, TX

#include "SparkFun_UHF_RFID_Reader.h" //Library for controlling the M6E Nano module
RFID nano; //Create instance

#include <WiFi.h>
#include "time.h"

const char* ssid     = "UPCFEE6AA4";
const char* password = "3e7syffdjAvF";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;
int i=0;

struct tm timeinfo;

void array_to_string(byte array[], unsigned int len, char buffer[]) // everything read is saved as byte array - this is to save EPC as string for printing and JSON purposes
{
    for (unsigned int i = 0; i < len; i++)
    {
        byte nib1 = (array[i] >> 4) & 0x0F;
        byte nib2 = (array[i] >> 0) & 0x0F;
        buffer[i*2+0] = nib1  < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
        buffer[i*2+1] = nib2  < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
    }
    buffer[len*2] = '\0';
}

boolean setupNano(long baudRate)
{
  nano.begin(softSerial); //Tell the library to communicate over software serial port
  //nano.enableDebugging();
  //Test to see if we are already connected to a module
  //This would be the case if the Arduino has been reprogrammed and the module has stayed powered
  softSerial.begin(baudRate); //For this test, assume module is already at our desired baud rate
  while (!softSerial); //Wait for port to open

  //About 200ms from power on the module will send its firmware version at 115200. We need to ignore this.
  while (softSerial.available()) softSerial.read();

  nano.getVersion();

  if (nano.msg[0] == ERROR_WRONG_OPCODE_RESPONSE)
  {
    //This happens if the baud rate is correct but the module is doing a ccontinuous read
    nano.stopReading();

    Serial.println(F("Module continuously reading. Asking it to stop..."));

    delay(1500);
  }
  else
  {
    //The module did not respond so assume it's just been powered on and communicating at 115200bps
    softSerial.begin(115200); //Start software serial at 115200

    nano.setBaud(baudRate); //Tell the module to go to the chosen baud rate. Ignore the response msg

    softSerial.begin(baudRate); //Start the software serial port, this time at user's chosen baud rate

    delay(250);
  }

  //Test the connection
  nano.getVersion();
  if (nano.msg[0] != ALL_GOOD) return (false); //Something is not right

  //The M6E has these settings no matter what
  nano.setTagProtocol(); //Set protocol to GEN2

  nano.setAntennaPort(); //Set TX/RX antenna ports to 1

  return (true); //We are ready to rock
}

void setup()
{
  Serial.begin(115200);
  
  Serial.println("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); //Synchronization 
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo);

  while (!Serial);
  Serial.println();
  Serial.println("Initializing...");

  if (setupNano(38400) == false) //Configure nano to run at 38400bps
  {
    Serial.println("Module failed to respond. Please check wiring.");
    while (1); //Freeze!
  }

  nano.setRegion(REGION_EUROPE); //Set to correct Region - only difference between markets

  nano.setReadPower(500); //5.00 dBm. Higher values may cause USB port to brown out
  //Max Read TX Power is 27.00 dBm and may cause temperature-limit throttling
}

void loop()
{
  for (;i<1000;i++){ //for prototyping purposes set to 1000 - final product should have ca. 10k reads.
  byte myEPC[12]; //EPCs are 12 bytes
  byte myEPClength;
  byte responseType = 0;

  while (responseType != RESPONSE_SUCCESS)//RESPONSE_IS_TAGFOUND)
  {
    myEPClength = sizeof(myEPC); //Length of EPC is modified each time .readTagEPC is called

    responseType = nano.readTagEPC(myEPC, myEPClength); //Scan for a new tag
    //Serial.println(responseType);
    //Serial.println(F("Searching for tag"));
  }
  if (i%50==0){
    Serial.print(i);
    Serial.print(" reads done and ");
    Serial.print(productSet.size());
    Serial.println(" tags found");
  }
  //Serial.println(responseType);
  //Serial.println("Odczytano!");
  char strEPC[32] = "";
  array_to_string(myEPC, myEPClength, strEPC);
  //Serial.print("Odczytane EPC: ");
  //Serial.println(myEPClength);
  //Serial.println(strEPC);
  productSet.insert(strEPC);

  //Print EPC
  /*Serial.print(F(" epc["));
  for (byte x = 0 ; x < myEPClength ; x++)
  {
    if (myEPC[x] < 0x10) Serial.print(F("0"));
    Serial.print(myEPC[x], HEX);
    Serial.print(F(" "));
  }
  Serial.println(F("]"));*/
}
if (productSet.size() > 0){
    Serial.println("Tags found:");
    for( auto iter = productSet.begin(); iter != productSet.end();iter++ )
         Serial.println(*iter);
    Serial.println("End of set");
    productSet.clear();
    esp_sleep_enable_timer_wakeup(1000000*30);
    esp_deep_sleep_start();
    i = 0;
    }
  
}


void setup()
{
  Serial.begin(115200);
  while (!Serial); //Wait for the serial port to come online
  
  Serial.println("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo);

  if (setupNano(38400) == false) //Configure nano to run at 38400bps
  {
    Serial.println(F("Module failed to respond. Please check wiring."));
    while (1); //Freeze!
  }

  nano.setRegion(REGION_EUROPE); //Set to North America

  nano.setReadPower(500); //5.00 dBm. Higher values may caues USB port to brown out
  //Max Read TX Power is 27.00 dBm and may cause temperature-limit throttling

  Serial.println(F("Press a key to begin scanning for tags."));
  while (!Serial.available()); //Wait for user to send a character
  Serial.read(); //Throw away the user's character

  nano.startReading(); //Begin scanning for tags
  
}

void loop()
{
  for(;i<500;i++)  //Constant read 100.000 times should cover all in range
  {
    delay(50);
    byte byteEPC[12]; //Most EPCs are 12 bytes
    byte myEPClength;
    byte responseType = 0;
    if (responseType != RESPONSE_SUCCESS){
      myEPClength = sizeof(byteEPC); //Length of EPC is modified each time .readTagEPC is called
      responseType = nano.readTagEPC(byteEPC, myEPClength, 500);
      Serial.println(responseType);
    }
    else if (responseType == RESPONSE_SUCCESS){
      //nano.enableDebugging();
      //Scan for a new tag up to 500ms
      Serial.println(responseType);
      Serial.println("Odczytano!");
      char strEPC[32] = "";
      array_to_string(byteEPC, myEPClength, strEPC);
      Serial.print("Odczytane EPC: ");
      Serial.println(myEPClength);
      Serial.println(strEPC);
      productSet.insert(strEPC);
      
      //Print EPC bytes, this is a subsection of bytes from the response/msg array
      Serial.print(F(" epc["));
      for (byte x = 0 ; x < myEPClength ; x++)
      {
        if (byteEPC[x] < 0x10) Serial.print(F("0"));
        Serial.print(byteEPC[x], HEX);
        Serial.print(F(" "));
      }
      Serial.println(F("]"));
      //nano.disableDebugging();
    }
    else if (responseType == RESPONSE_IS_KEEPALIVE) {
      Serial.println("No tags found! Either none is nearby or something went wrong!");
    }
  }
    if (productSet.size() > 1){
    for( auto iter = productSet.begin(); iter != productSet.end(); ++iter )
         Serial.println(*iter);
    //i = 0;
    }
}