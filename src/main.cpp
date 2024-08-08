#include <Arduino.h>
#include <SoftwareSerial.h> //Used for transmitting to the device
#include <set>
#include <string>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include "FS.h"
#include <PNGdec.h>
#include "logo.h"
#include <TFT_eSPI.h>
#include <time.h>
#include <WiFi.h>
#include "SparkFun_UHF_RFID_Reader.h" //Library for controlling the M6E Nano module

TFT_eSPI tft = TFT_eSPI(); // Invoke custom library
PNG png;
#define CALIBRATION_FILE "/calibrationData"
#define REPEAT_CAL false
#define NELEMS(x) (sizeof(x) / sizeof((x)[0]))
#define MAX_IMAGE_WDITH 240
#define BUTTON_PIN 16
int16_t xpos = 0;
int16_t ypos = 0;
std::set<String> productSet;
SoftwareSerial softSerial(26, 25); // RX, TX

RFID nano; // Create instance

const char *ssid = "UPCFEE6AA4";
const char *password = "3e7syffdjAvF";

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;
int i = 0;
int j = 0;

struct tm timeinfo;
int x, y; // Coordinates for drawing
struct product
{
  const char *productName;
  const char *brandName;
  const char *expDate;
  const char *expired;
};
product prod[10];

void touch_calibrate();
void setProductCursor(unsigned int pos);
void drawLayout();
void printProductInfo(char const *product, char const *brand, const char *date, const char *expired, uint8_t pos);
void pngDraw(PNGDRAW *pDraw);
void array_to_string(byte array[], unsigned int len, char buffer[]);
boolean setupNano(long baudRate);
void parseJsonResponse(const String &jsonResponse, product *products, size_t arraySize);
void sendEpcSetToServer(const std::set<String> &epcSet);
void RecognitionModeScan();
void scanTags();
unsigned long getTime();
time_t convertToUnixTime(const char *dateString);

void setup()
{
  uint16_t calibrationData[5];
  uint8_t calDataOK = 0;
  Serial.begin(115200);
  tft.init();
  tft.setRotation(3);
  touch_calibrate();

  tft.fillScreen(TFT_WHITE);

  tft.setTextColor(TFT_BLACK, TFT_NOP, true);
  tft.setTextSize(2);
  tft.setTextDatum(TL_DATUM);

  int16_t rc = png.openFLASH((uint8_t *)logo, sizeof(logo), pngDraw);
  if (rc == PNG_SUCCESS)
  {
    Serial.println("Successfully loaded png file");
    Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
    tft.startWrite();
    uint32_t dt = millis();
    rc = png.decode(NULL, 0);
    Serial.print(millis() - dt);
    Serial.println("ms");
    tft.endWrite();
    // png.close(); // not needed for memory->memory decode
  }
  delay(3000);
  tft.fillScreen(TFT_WHITE);
  drawLayout();
  Serial.println("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); // Synchronization
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo);
  time_t oneday = 86400;
  time_t date = mktime(&timeinfo);
  time_t days = date % oneday;
  struct tm *ptm = gmtime(&date);
  char buf[256] = {0};
  strftime(buf, 256, "%s", ptm);
  Serial.printf(buf);

  while (!Serial)
    ;
  Serial.println();
  Serial.println("Initializing...");

  if (setupNano(38400) == false) // Configure nano to run at 38400bps
  {
    Serial.println("Module failed to respond. Please check wiring.");
    while (1)
      ; // Freeze!
  }

  nano.setRegion(REGION_EUROPE); // Set to correct Region - only difference between markets

  nano.setReadPower(1200); // 20.00 dBm
  // Max Read TX Power is 27.00 dBm and may cause temperature-limit throttling

  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void loop()
{
  if (!digitalRead(BUTTON_PIN))
  {
    Serial.println("Recognition mode. Please place only one tag in the read area");
    RecognitionModeScan();
  }
  uint16_t t_x = 0, t_y = 0;
  bool pressed = tft.getTouch(&t_x, &t_y);
  tft.setCursor(0, 0);
  if (i == 0)
  {
    tft.fillRect(260, 0, 60, 60, TFT_DARKGREY);
    tft.fillTriangle(260 + 30, 5, 265, 55, 320 - 5, 55, TFT_LIGHTGREY);
    tft.drawTriangle(260 + 30, 5, 265, 55, 320 - 5, 55, TFT_BLACK);
    tft.drawTriangle(260 + 30 - 1, 6, 264, 54, 320 - 6, 54, TFT_DARKGREY);
  }
  if (i >= NELEMS(prod) - 4)
  {
    tft.fillRect(260, 180, 60, 240, TFT_DARKGREY);
    tft.fillTriangle(260 + 30, 240 - 5, 265, 240 - 55, 320 - 5, 240 - 55, TFT_LIGHTGREY);
    tft.drawTriangle(260 + 30, 240 - 5, 265, 240 - 55, 320 - 5, 240 - 55, TFT_BLACK);
    tft.drawTriangle(260 + 30 - 1, 240 - 6, 264, 240 - 54, 320 - 6, 240 - 54, TFT_DARKGREY);
  }

  if (pressed && t_x >= 260 && t_y < 60 && i >= 1)
  { // TFT: Up button touched
    i--;
  }
  if (pressed && t_x >= 260 && t_y > 180 && i < NELEMS(prod) - 4)
  { // TFT: down button touched
    i++;
  }
  if (pressed && t_x >= 260 && t_y > 60 && t_y < 180)
  { // TFT: scan area touched
    scanTags();
    drawLayout();
  }
  if (pressed && i > 0 && i < NELEMS(prod) - 4)
    drawLayout();

  tft.fillRect(0, 0, 260, 59, TFT_WHITE);
  printProductInfo(prod[i].productName, prod[i].brandName, prod[i].expDate, prod[i].expired, 0);
  tft.fillRect(0, 61, 260, 59, TFT_WHITE);
  printProductInfo(prod[i + 1].productName, prod[i + 1].brandName, prod[i + 1].expDate, prod[i + 1].expired, 1);
  tft.fillRect(0, 121, 260, 59, TFT_WHITE);
  printProductInfo(prod[i + 2].productName, prod[i + 2].brandName, prod[i + 2].expDate, prod[i + 2].expired, 2);
  tft.fillRect(0, 181, 260, 59, TFT_WHITE);
  printProductInfo(prod[i + 3].productName, prod[i + 3].brandName, prod[i + 3].expDate, prod[i + 3].expired, 3);
  pressed = false;
}

void touch_calibrate()
{
  uint16_t calData[5];
  uint8_t calDataOK = 0;

  // check file system exists
  if (!SPIFFS.begin())
  {
    Serial.println("Formating file system");
    SPIFFS.format();
    SPIFFS.begin();
  }

  // check if calibration file exists and size is correct
  if (SPIFFS.exists(CALIBRATION_FILE))
  {
    if (REPEAT_CAL)
    {
      // Delete if we want to re-calibrate
      SPIFFS.remove(CALIBRATION_FILE);
    }
    else
    {
      File f = SPIFFS.open(CALIBRATION_FILE, "r");
      if (f)
      {
        if (f.readBytes((char *)calData, 14) == 14)
          calDataOK = 1;
        f.close();
      }
    }
  }

  if (calDataOK && !REPEAT_CAL)
  {
    // calibration data valid
    tft.setTouch(calData);
  }
  else
  {
    // data not valid so recalibrate
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 0);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.println("Touch corners as indicated");

    tft.setTextFont(1);
    tft.println();

    if (REPEAT_CAL)
    {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println("Set REPEAT_CAL to false to stop this running again!");
    }

    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("Calibration complete!");

    // store data
    File f = SPIFFS.open(CALIBRATION_FILE, "w");
    if (f)
    {
      f.write((const unsigned char *)calData, 14);
      f.close();
    }
  }
}

void setProductCursor(unsigned int pos)
{
  switch (pos)
  {
  case 0:
    tft.setCursor(10, 20);
    break;
  case 1:
    tft.setCursor(10, 80);
    break;
  case 2:
    tft.setCursor(10, 135);
    break;
  case 3:
    tft.setCursor(10, 195);
    break;
  default:
    tft.setCursor(10, 195);
    break;
  }
}
void drawLayout()
{
  tft.drawLine(260, 0, 260, 320, TFT_BLACK);
  tft.drawLine(0, 60, 320, 60, TFT_BLACK);
  tft.fillRectVGradient(260, 0, 60, 60, TFT_SKYBLUE, TFT_NAVY); // Up-button: 60x60px, x=260 y=0
  tft.fillTriangle(260 + 30, 5, 265, 55, 320 - 5, 55, TFT_LIGHTGREY);
  tft.drawTriangle(260 + 30, 5, 265, 55, 320 - 5, 55, TFT_BLACK);
  tft.drawTriangle(260 + 30 - 1, 6, 264, 54, 320 - 6, 54, TFT_DARKGREY);
  tft.fillRect(260, 61, 60, 120, TFT_NAVY);
  tft.fillRectVGradient(260, 180, 60, 240, TFT_NAVY, TFT_SKYBLUE);
  tft.fillTriangle(260 + 30, 240 - 5, 265, 240 - 55, 320 - 5, 240 - 55, TFT_LIGHTGREY);
  tft.drawTriangle(260 + 30, 240 - 5, 265, 240 - 55, 320 - 5, 240 - 55, TFT_BLACK);
  tft.drawTriangle(260 + 30 - 1, 240 - 6, 264, 240 - 54, 320 - 6, 240 - 54, TFT_DARKGREY);
  tft.drawLine(0, 120, 260, 120, TFT_BLACK);
  tft.drawLine(0, 180, 320, 180, TFT_BLACK);
}
void printProductInfo(char const *product, char const *brand, const char *date, const char *expired, uint8_t pos)
{
  tft.setTextSize(2);
  setProductCursor(pos);
  tft.print(product);
  tft.setTextSize(1);
  tft.setCursor(30, tft.getCursorY() + 30);
  tft.print(brand);
  tft.setCursor(140, tft.getCursorY());
  tft.print("Exp: ");
  tft.print(date); // Display formatted date
}
void pngDraw(PNGDRAW *pDraw)
{
  uint16_t lineBuffer[MAX_IMAGE_WDITH];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(40, ypos + pDraw->y, pDraw->iWidth, 1, lineBuffer);
}
void array_to_string(byte array[], unsigned int len, char buffer[]) // everything read is saved as byte array - this is to save EPC as string for printing and JSON purposes
{
  for (unsigned int i = 0; i < len; i++)
  {
    byte nib1 = (array[i] >> 4) & 0x0F;
    byte nib2 = (array[i] >> 0) & 0x0F;
    buffer[i * 2 + 0] = nib1 < 0xA ? '0' + nib1 : 'A' + nib1 - 0xA;
    buffer[i * 2 + 1] = nib2 < 0xA ? '0' + nib2 : 'A' + nib2 - 0xA;
  }
  buffer[len * 2] = '\0';
}

boolean setupNano(long baudRate)
{
  Serial.println("wszedłem w setup");
  nano.begin(softSerial); // Tell the library to communicate over software serial port
  // nano.enableDebugging();
  // Test to see if we are already connected to a module
  // This would be the case if the Arduino has been reprogrammed and the module has stayed powered
  softSerial.begin(baudRate); // For this test, assume module is already at our desired baud rate
  Serial.println("sprawdzam baud rate modulu");
  while (!softSerial)
    ; // Wait for port to open
  Serial.println("port otwarty");

  // About 200ms from power on the module will send its firmware version at 115200. We need to ignore this.
  while (softSerial.available())
    softSerial.read();

  nano.getVersion();
  Serial.println("wyciagnalem wersje i sie nie zjebalem");
  if (nano.msg[0] == ERROR_WRONG_OPCODE_RESPONSE)
  {
    // This happens if the baud rate is correct but the module is doing a ccontinuous read
    nano.stopReading();

    Serial.println(F("Module continuously reading. Asking it to stop..."));

    delay(1500);
  }
  else
  {
    // The module did not respond so assume it's just been powered on and communicating at 115200bps
    softSerial.begin(115200); // Start software serial at 115200

    nano.setBaud(baudRate); // Tell the module to go to the chosen baud rate. Ignore the response msg

    softSerial.begin(baudRate); // Start the software serial port, this time at user's chosen baud rate

    delay(250);
  }

  // Test the connection
  nano.getVersion();
  if (nano.msg[0] != ALL_GOOD)
    return (false); // Something is not right

  // The M6E has these settings no matter what
  nano.setTagProtocol(); // Set protocol to GEN2

  nano.setAntennaPort(); // Set TX/RX antenna ports to 1
  Serial.println("dupsko");

  return (true); // We are ready to rock
}

void parseJsonResponse(const String &jsonResponse, product *products, size_t arraySize)
{
  // Parse the JSON document
  DynamicJsonDocument jsonDoc(1024);
  DeserializationError error = deserializeJson(jsonDoc, jsonResponse);

  // Check for parsing errors
  if (error)
  {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Extract the array of decoded_products
  JsonArray decodedProducts = jsonDoc["products"];

  // Iterate over each product in the array
  size_t i = 0;
  for (JsonObject productObj : decodedProducts)
  {
    // Populate the product struct
    products[i].productName = productObj["product_name"];
    products[i].brandName = productObj["brand_name"];
    products[i].expDate = productObj["expiry_date"];
    products[i].expired = productObj["expired"];

    // Move to the next element in the array
    i++;

    // Break if we reach the end of the array
    if (i >= arraySize)
    {
      break;
    }
  }
}

void sendEpcSetToServer(const std::set<String> &epcSet)
{
  // Create a JSON document
  DynamicJsonDocument jsonDoc(1024); // Adjust the size according to your data

  // Create an array within the JSON document
  JsonArray epcArray = jsonDoc.createNestedArray("epcs");

  // Add each EPC code to the array
  for (const auto &epc : epcSet)
  {
    epcArray.add(epc);
  }

  // Serialize the JSON document to a string
  String jsonString;
  serializeJson(jsonDoc, jsonString);

  // Specify the server URL
  const char *serverUrl = "https://smellySocks.pythonanywhere.com/fridgeAPI/receive-epcs/";

  // Use HTTPClient to send the JSON data to the server
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  Serial.println("JSON to be posted:");
  Serial.println(jsonString);
  // Send the JSON data to the server
  int httpResponseCode = http.POST(jsonString);

  // Check for a successful response
  if (httpResponseCode > 0)
  {
    Serial.printf("HTTP POST Success, Response code: %d\n", httpResponseCode);
  }
  else
  {
    Serial.printf("HTTP POST Failed, Error code: %d\n", httpResponseCode);
  }
  String jsonResponse = http.getString();
  Serial.println("Response:");
  Serial.println(jsonResponse);
  parseJsonResponse(jsonResponse, prod, sizeof(prod) / sizeof(prod[0]));
  // Close the connection
  http.end();
}

void scanTags()
{
  Serial.println("szukam tagow");
  nano.startReading();
  for (j=0; j < 100; j++) // for prototyping purposes set to 100
  { 
    
    byte responseType = nano.parseResponse();
    String strEPC = "";

    if (nano.check() == true) //check if module responds anyhow
    {
      strEPC = "";
      responseType = nano.parseResponse();
      if (responseType == RESPONSE_IS_TAGFOUND)
      {
        byte tagEPCBytes = nano.getTagEPCBytes();
        for (byte x = 0; x < tagEPCBytes; x++)
        {
          strEPC += String(nano.msg[31 + x], HEX);
        }
        Serial.print(strEPC);
        // array_to_string(myEPC, myEPClength, strEPC);
        strEPC.toUpperCase();
        productSet.insert(strEPC);
      }
    }

    // if (j%50==0){
    Serial.print(j);
    Serial.print(" reads done and ");
    Serial.print(productSet.size());
    Serial.println(" tags found");
    // }
  }
  nano.stopReading();
  if (productSet.size() > 0)
  {
    Serial.println("Tags found:");
    for (auto iter = productSet.begin(); iter != productSet.end(); iter++)
      Serial.println(*iter);
    Serial.println("End of set, sending to server");
    sendEpcSetToServer(productSet);
    productSet.clear();
    // esp_sleep_enable_timer_wakeup(1000000*30);
    // esp_deep_sleep_start();
    j = 0;
  }
   tft.fillRect(0, 0, 260, 59, TFT_WHITE);
  printProductInfo(prod[i].productName, prod[i].brandName, prod[i].expDate, prod[i].expired, 0);
  tft.fillRect(0, 61, 260, 59, TFT_WHITE);
  printProductInfo(prod[i + 1].productName, prod[i + 1].brandName, prod[i + 1].expDate, prod[i + 1].expired, 1);
  tft.fillRect(0, 121, 260, 59, TFT_WHITE);
  printProductInfo(prod[i + 2].productName, prod[i + 2].brandName, prod[i + 2].expDate, prod[i + 2].expired, 2);
  tft.fillRect(0, 181, 260, 59, TFT_WHITE);
  printProductInfo(prod[i + 3].productName, prod[i + 3].brandName, prod[i + 3].expDate, prod[i + 3].expired, 3);
}

void RecognitionModeScan()
{
  byte responseType;
  nano.startReading();

  // Read until tag found
  while (true)
  {
    if (nano.check() == true)
    {
      responseType = nano.parseResponse();
      if (responseType == RESPONSE_IS_TAGFOUND)
      {
        Serial.println("tagfound");
        break;
      }
    }
  }
  nano.stopReading();
  String strEPC = "";
  byte tagEPCBytes = nano.getTagEPCBytes();
  for (byte x = 0; x < tagEPCBytes; x++)
  {
    strEPC += String(nano.msg[31 + x], HEX);
  }
  Serial.print(strEPC);
  strEPC.toUpperCase();
  StaticJsonDocument<200> jsonDoc;
  jsonDoc["epc"] = strEPC;

  // Display JSON to be posted
  serializeJsonPretty(jsonDoc, Serial);

  String jsonString;
  serializeJson(jsonDoc, jsonString);
  const char *serverUrl = "https://smellySocks.pythonanywhere.com/fridgeAPI/recognition_mode/";
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  // Send the JSON data to the server
  int httpResponseCode = http.POST(jsonString);

  // Check for a successful response
  if (httpResponseCode > 0)
  {
    Serial.printf("HTTP POST Success, Response code: %d\n", httpResponseCode);
  }
  else
  {
    Serial.printf("HTTP POST Failed, Error code: %d\n", httpResponseCode);
  }
  drawLayout();
}

unsigned long getTime()
{
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    // Serial.println("Failed to obtain time");
    return (0);
  }
  time(&now);
  return now;
}

time_t convertToUnixTime(const char *dateString)
{
  struct tm tmInfo;

  // Parse the date string
  if (sscanf(dateString, "%d/%d/%d", &tmInfo.tm_mday, &tmInfo.tm_mon, &tmInfo.tm_year) != 3)
  {
    // Parsing failed
    Serial.println("Failed to parse date string");
    return 0; // Return 0 or another appropriate value to indicate an error
  }

  // Adjust tm structure members
  tmInfo.tm_year -= 1900; // Year is offset from 1900
  tmInfo.tm_mon -= 1;     // Month is zero-based

  // Set other tm structure members to default values
  tmInfo.tm_hour = 0;
  tmInfo.tm_min = 0;
  tmInfo.tm_sec = 0;
  tmInfo.tm_isdst = -1;

  // Convert the struct tm to time_t
  time_t result = mktime(&tmInfo);

  return result;
}