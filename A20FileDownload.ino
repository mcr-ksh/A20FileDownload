/**************************************************************
 *
 * GPLv3
 *
 * Copyright (C), 2017 ksh@ironsoftware.de
 *
 **************************************************************/
using namespace std;
#include <assert.h>

// Plattform
#include <Arduino.h>
#include <ESP8266WiFi.h>

// Select your modem:
//#define TINY_GSM_MODEM_SIM800
//#define TINY_GSM_MODEM_SIM900
#define TINY_GSM_MODEM_A6
//#define TINY_GSM_MODEM_M590

// Increase RX buffer
#define TINY_GSM_RX_BUFFER 2048
#define TINY_GSM_DEBUG    Serial

#include <TinyGsmClient.h>
#include <StreamDebugger.h>
#include <CRC32.h>


//#define WIFI


const char* ssid     = "";
const char* password = "";

// Your GPRS credentials
// Leave empty, if missing user or pass
const char apn[]  = "internet";
const char user[] = "";
const char pass[] = "";


// Use Hardware Serial on Mega, Leonardo, Micro
//#define SerialAT Serial1
// or Software Serial on Uno, Nano
#include <SoftwareSerial.h>
SoftwareSerial SerialA20(13, 15); // RX, TX

//StreamDebugger  gsmdebugger(SerialA20, Serial);
//TinyGsm         modem(gsmdebugger);
TinyGsm       modem(SerialA20);

#ifndef WIFI
 TinyGsmClient  client(modem);
#else
 WiFiClient     client;
#endif

const char server[] = "kiwi.ironsoftware.de";
const char resource[] = "/dltest";
const char userAgent[] = "A20demo/1/dummy";
uint32_t knownCRC32 = 0x6f6cd850;
#define  HTTP_CHUNK_SIZE      1024

#define DEBUG
#ifdef DEBUG
  bool log_it_all                 = false;
  void *debug_ptr                 = NULL;
  void DebugPrint (const char *format, ...)
  {
    static char sbuf[1400];                                                     // For debug lines
  
    va_list varArgs;                                                            // For variable number of params
  
    va_start ( varArgs, format );                                               // Prepare parameters
    vsnprintf ( sbuf, sizeof ( sbuf ), format, varArgs );                       // Format the message
    va_end ( varArgs );                                                         // End of using parameters
  
    Serial.print ( sbuf );
  }
  
  #define ErrorLog(...)     { log_it_all=true; DebugPrint(__VA_ARGS__); log_it_all=false; }  
  
  void DebugDump(const void *addr, uint32_t c)
  {
    const byte *ptr = (const byte *)(const void *)addr;
    for (int j = 0; j < c; ) {
      DebugPrint("%d:\t", j);
      for (int i = 0; i < min(16, (int)c); i++) {
        DebugPrint(" %02X", *(ptr++));
        j++;
      }
      DebugPrint("\n");
    }
  }
#else
  #define ErrorLog( ... )
  #define DebugPrint( ... )
  #define DebugDump( ... )
#endif

#ifdef DEBUG2
  #define DebugPrint2(...)     { DebugPrint(__VA_ARGS__); }
#else
  #define DebugPrint2( ... )
#endif

// handle diagnostic informations given by assertion and abort program execution:
void __assert(const char *__func, const char *__file, int __lineno, const char *__sexp) {
    // transmit diagnostic informations through serial link. 
    Serial.println(__func);
    Serial.println(__file);
    Serial.println(__lineno, DEC);
    Serial.println(__sexp);
    Serial.flush();
    // abort program execution.
    abort();
}

void setup() 
{
  // Set console baud rate
  Serial.begin(115200);
  delay(10);

#ifdef WIFI
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  DebugPrint("\nWiFi Connected.\n");
#else
  // Set GSM module baud rate
  SerialA20.begin(57600);
  delay(3000);

  // Restart takes quite some time
  // To skip it, call init() instead of restart()
  DebugPrint("Initializing modem...\n");
  modem.restart();

  // Unlock your SIM card with a PIN
  modem.simUnlock("6450");

  DebugPrint("Waiting for network...");
  if (!modem.waitForNetwork()) {
    DebugPrint(" fail\n");
    delay(10000);
    return;
  }
  DebugPrint(" done\n");

  DebugPrint("Connecting to ");
  DebugPrint(apn);
  if (!modem.gprsConnect(apn, user, pass)) {
    DebugPrint(" fail\n");
    while ( true ) delay(10000);
    return;
  }
  DebugPrint(" done\n");

  /* recharge */
  if ( false ) {
    modem.sendAT(GF("AT+CSCS=\"HEX\""));                           // AT+CSCS="HEX"
    modem.waitResponse();
    modem.sendAT(GF("AT+CUSD=1,\"*103*4918048533142449#\",15"));   // AT+CUSD=1,"*103*4918048533142449#",15
    modem.waitResponse();
    DebugPrint("setup(): recharging: %s\n", SerialA20.readString().c_str());
    modem.sendAT(GF("AT+CUSD=1,\"*101#\",15"));                    // AT+CUSD=1,"*101#",15
    DebugPrint("setup(): recharging: %s\n", SerialA20.readString().c_str());    
    modem.waitResponse();
  }  
#endif  

}

void printPercent(uint32_t readLength, uint32_t contentLength) {
  // If we know the total length
  if (contentLength != -1) {
    DebugPrint("\r %d%%", readLength/contentLength*100);
  } else {
    DebugPrint("%d", readLength);
  }
}

int http_read_content_length(Client &client)
{
  uint32_t contentLength = -1;
  
  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if ( line.startsWith("HTTP/1.1") && !line.startsWith("HTTP/1.1 20")) {  /* 200 & 206 */
      Serial.println(line);
      DebugPrint("http_read_content_length(): wrong statusCode: %s", line.c_str());
      break;
    }
    line.toLowerCase();
    if (line.startsWith("content-length:")) {
      contentLength = line.substring(line.lastIndexOf(':') + 1).toInt();  
    } else if (line.length() == 0) {
      break;
    }
  }

  return contentLength;
}

void HttpFinish()
{
  return;
  
  SerialA20.println("AT+CIPSTATUS");
  DebugPrint("CIPSTATUS(): %s\n", SerialA20.readString().c_str());
  modem.sendAT(GF("+CIPCLOSE"));
  if (modem.waitResponse() != 1) {
    return;
  } 
}

int HttpGetContentLength()
{
  DebugPrint("HttpGetContentLength(): -->\n");
  
  DebugPrint("HttpGetContentLength(): Connecting to %s... ", server);
  
  // if you get a connection, report back via serial:
  if (!client.connect(server, 80)) {
    DebugPrint(" fail\n");
    delay(10000);
    return -1;
  }
  DebugPrint(" done\n");
  // Make a HTTP request:
  client.print(String("HEAD ") + resource + " HTTP/1.1\r\n");
  client.print(String("Host: ") + server + "\r\n");
  client.print(String("User-Agent: ") + userAgent + "\r\n");
  client.print("Connection: close\r\n\r\n");

  long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000L) {
      DebugPrint(">>> Client Timeout !\n");
      client.stop();
#ifndef WIFI  
      HttpFinish();
#endif
      delay(10000L);
      return -1;
    }
  }

  DebugPrint("HttpGetContentLength(): Reading response header\n");
  uint32_t contentLength = http_read_content_length(client);
  
  client.stop();
  DebugPrint("HttpGetContentLength(): Server disconnected\n");
#ifndef WIFI  
  HttpFinish();
#endif  

  DebugPrint("HttpGetContentLength(): <-- contentLength = %d\n", contentLength);
  
  return contentLength;
}

int HttpGetRange(uint8_t *data, size_t size, uint32_t startRange)
{
  DebugPrint("HttpGetRange(): -->\n");
  
  DebugPrint("HttpGetRange(): Connecting to %s ...", server);
  
  // if you get a connection, report back via serial:
  if (!client.connect(server, 80)) {
    DebugPrint(" fail\n");
    delay(10000);
    return -3;
  }
  DebugPrint(" done\n");

  DebugPrint("HttpGetRange(): Requesting Range of %d - %d (%d bytes)\n", startRange, startRange+size-1, size);
  
  // Make a HTTP request:
  client.print(String("GET ") + resource + " HTTP/1.1\r\n");
  client.print(String("Host: ") + server + "\r\n");
  client.print(String("User-Agent: ") + userAgent + "\r\n");  
  client.print(String("Range: bytes=") + startRange + "-" + String(startRange+size-1) + "\r\n");
  client.print("Connection: close\r\n\r\n");

  long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000L) {
      DebugPrint(">>> Client Timeout !\n");
      client.stop();
#ifndef WIFI  
      HttpFinish();
#endif  
      delay(10000L);
      return -2;
    }
  }

  DebugPrint("HttpGetRange(): Reading response header\n");
  uint32_t contentLength = http_read_content_length(client);
  if ( contentLength == -1 || contentLength != size ) {
    DebugPrint("HttpGetRange(): contentLength problem: %d (requested: %d)\n", contentLength, size);
    return -1;
  }
  
  DebugPrint("HttpGetRange(): Reading response data\n");
  timeout = millis();
  uint32_t readLength = 0;  

  // read all data from server
  while (readLength < contentLength && client.connected() && millis() - timeout < 30000L) {
    // read up to TINY_GSM_RX_BUFFER byte
    int c = client.available();
    if ( !c ) { 
      delay(1); 
      continue;
    }
 
    int ret = client.read(data+readLength, c);
    if ( ret != c ) {
      DebugPrint("read problem.\n");
      break;
    }    
    DebugPrint2("read: from readLength = %d to %d (%d bytes)\n", readLength, readLength+ret, ret);      

    readLength += ret;
    timeout = millis();
  }
  client.stop(); 
  DebugPrint("HttpGetRange(): Server disconnected\n");

#ifndef WIFI  
  HttpFinish();
#endif

  DebugPrint("HttpGetRange(): <-- readLength = %d\n", readLength);

  return readLength;
}

void loop() 
{

/* DOWNLOAD */

  /* actual chunked download */
  CRC32 crc;
  uint32_t readLength = 0;

  /* sanity check */
  //assert(TINY_GSM_RX_BUFFER > HTTP_CHUNK_SIZE);

  /* allocate memory */
  uint8_t *data = (uint8_t *)malloc(HTTP_CHUNK_SIZE);
  if (!data) {
    DebugPrint("memory allocation problem\n");
    return;
  }

  /* total size to download */
  int32_t contentLength = HttpGetContentLength();
  if ( contentLength == -1 ) {
    DebugPrint("contentLength problem\n");
    free(data);
    return;
  }

  /* download chunks until total size reached */
  long timeout = millis();
  unsigned long timeElapsed = millis();
  while (readLength < contentLength && millis() - timeout < 600000L) { /* 10 min timeout */
    int toread = (contentLength - readLength > HTTP_CHUNK_SIZE ? HTTP_CHUNK_SIZE : contentLength - readLength);
    int ret = HttpGetRange(data, toread, readLength);
    if ( ret != toread ) {
      DebugPrint("ERROR: read buf size does not match, ret = %d (requested = %d).\n", ret, toread);
      continue;
    }

    for (size_t i = 0; i < ret; i++)
    {
      crc.update(data[i]);
      if (readLength+i % (contentLength / 13) == 0) {
        printPercent(readLength+i, contentLength);
      }
    }
    readLength += ret;
    timeout = millis();
  }
  free(data);
  printPercent(readLength, contentLength);
  DebugPrint("\n");
  timeElapsed = millis() - timeElapsed;
  
  if (timeElapsed >= 60000L)
    DebugPrint("Timeout.\n");

  /* STATS */
  float duration = float(timeElapsed) / 1000;
  DebugPrint("Content-Length: %d\n", contentLength);
  DebugPrint("Actually read:  %d\n", readLength);
  DebugPrint("Calc. CRC32:    0x%x\n", crc.finalize());
  DebugPrint("Known CRC32:    0x%x\n", knownCRC32);
  DebugPrint("Duration:       %ds\n", (int32_t)duration);
    
/* DOWNLOAD/ */

  // Do nothing forevermore
  delay(10000);
}

