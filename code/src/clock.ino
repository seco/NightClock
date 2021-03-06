// https://github.com/adafruit/Adafruit_NeoPixel
// Platformio lib number 28
#include <Adafruit_NeoPixel.h>

// https://github.com/PaulStoffregen/Time
// Platformio lib number 28
#include <TimeLib.h>

// Time zone and daylight savings conversion, but cannot be used as-is
// ESP eeprom library not yet ported, so edited library to remove
// requirement on that library
// Original Library
// https://github.com/JChristensen/Timezone
// If it worked OOTB then would be Platformio lib number 76
// Forked version
// https://github.com/vanceb/Timezone
#include <Timezone.h>

// https://github.com/esp8266/arduino
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// Use WifiManager to allow easier setting of Wifi
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

// Enable OTA Software updates
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

// Define the parameters for the neopixels
#define NEO_PIN 13
#define NEO_NUMPIXELS 13

// NTP Servers:
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "1.uk.pool.ntp.org";
//IPAddress timeServer(132, 163, 4, 101); // time-a.timefreq.bldrdoc.gov
// IPAddress timeServer(132, 163, 4, 102); // time-b.timefreq.bldrdoc.gov
// IPAddress timeServer(132, 163, 4, 103); // time-c.timefreq.bldrdoc.gov

// Setup timezone and daylight saving correction library
TimeChangeRule ukGMT = {"GMT", Last, Sun, Oct, 2, 0};
TimeChangeRule ukBST = {"BST", Last, Sun, Mar, 2, 60};
Timezone ukTZ(ukGMT, ukBST);

// Create the Neopixel object
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NEO_NUMPIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);

// To allow for OTA reconfig of colours later on define a set of variables
uint32_t col_wifi_down = strip.Color(64,0,0);
uint32_t col_wifi_up = strip.Color(128,128,32);
uint32_t col_ntp_fail = strip.Color(64,64,0);
uint32_t col_hours = strip.Color(153,0,0);
uint32_t col_minutes = strip.Color(255,92,0);
uint32_t col_seconds = strip.Color(128,32,0);
uint32_t col_background = strip.Color(0,0,0);

uint8_t show_sec_brightness = 64;
uint8_t min_brightness = 5;
uint32_t last_updated = millis();
uint32_t time_now = millis();
uint8_t disp_update_period = 40;
uint8_t ntp_sync_interval = 60;

// Manage brightness with hysteresis
#define GO_BRIGHT 600
#define GO_DARK 700
bool isDark = false;

// Some status variables
bool ntp_fail = false;

// Create the UDP object
WiFiUDP Udp;

unsigned int localPort = 8888;  // local port to listen for UDP packets


void walk(uint32_t c, uint8_t wait){
    for(int i=0; i < strip.numPixels(); i++){
        strip.setPixelColor(i, c);
        strip.show();
        delay(wait);
        strip.setPixelColor(i, 0);
    }
}

void showStatus(){
    switch (WiFi.status()){
        case WL_CONNECTED :
        if(ntp_fail) {
            strip.setPixelColor(12, col_ntp_fail);
        } else {
            strip.setPixelColor(12, col_wifi_up);
        }
        break;
        default :
        strip.setPixelColor(12, col_wifi_down);
    }
}


void showTime() {
    time_t t = now();
    int hr_seg = (hour(t) + 6) % 12;
    int min_seg = ((minute(t) / 5) + 6) % 12;
    int sec_seg = ((second(t) / 5) + 6) % 12;
    for (int i=0; i<12; i++){
        if (i == hr_seg){
            strip.setPixelColor(i, col_hours);
        }
        if (i == min_seg) {
            strip.setPixelColor(i, mixColors(strip.getPixelColor(i), col_minutes));
        }
        if (i == sec_seg) {
            strip.setPixelColor(i, mixColors(strip.getPixelColor(i), col_seconds));
        }
        if (strip.getPixelColor(i) == 0) {
            strip.setPixelColor(i, col_background);
        }
    }
}


void fadeTime() {
    time_t t = ukTZ.toLocal(now());
    int hr_seg = (hour(t) + 6) % 12;
    int hr_fraction = minute(t);
    int min_seg = ((minute(t) / 5) + 6) % 12;
    int min_fraction = (minute(t) % 5)*60 + second(t);
    int sec_seg = ((second(t) / 5) + 6) % 12;
    int sec_fraction = ((second(t) % 5));
    for (int i=0; i<12; i++){
        if (i == hr_seg){
            strip.setPixelColor(i, fadeColor(col_hours, 60 - hr_fraction, 60));
        }
        if (i == (hr_seg + 1) % 12){
            strip.setPixelColor(i, mixColors(strip.getPixelColor(i), fadeColor(col_hours, hr_fraction, 60)));
        }
        if (i == min_seg) {
            strip.setPixelColor(i, mixColors(strip.getPixelColor(i), fadeColor(col_minutes, 300 - min_fraction, 300)));
        }
        if (i == (min_seg + 1) % 12) {
            strip.setPixelColor(i, mixColors(strip.getPixelColor(i), fadeColor(col_minutes, min_fraction, 300)));
        }
        if (strip.getBrightness() > show_sec_brightness) {
            if (i == sec_seg) {
                strip.setPixelColor(i, mixColors(strip.getPixelColor(i), fadeColor(col_seconds, 5-sec_fraction, 5)));
            }
            if (((i + 11) % 12) == sec_seg) {
                strip.setPixelColor(i, mixColors(strip.getPixelColor(i), fadeColor(col_seconds, sec_fraction, 5)));
            }
        }
        if (strip.getPixelColor(i) == 0) {
            strip.setPixelColor(i, col_background);
        }
    }
}

void ambientLight(){
    /*
    // Read LDR Value
    uint16_t light = analogRead(A0);
    uint8_t brightness = map(light, 0, 1023, 255, 0);
    brightness = constrain(brightness, min_brightness, 255);
    //Set Brightness of the Neopixels
    strip.setBrightness((uint8_t)brightness);
    */
    uint16_t light = analogRead(A0);
    uint8_t brightness;
    if (light > GO_DARK && !isDark) {
        isDark = true;
    } else if (light < GO_BRIGHT && isDark) {
        isDark = false;
    }
    if (isDark) {
        brightness = 32;
    } else {
        brightness = 255;
    }
    strip.setBrightness((uint8_t)brightness);
}

void updateDisplay(){
    ambientLight();
    strip.clear();
    showStatus();
    //    showTime();
    fadeTime();
    strip.show();
}

uint32_t mixColors(uint32_t c1, uint32_t c2) {
    uint8_t
    r1 = (uint8_t)(c1 >> 16),
    g1 = (uint8_t)(c1 >>  8),
    b1 = (uint8_t)c1,
    r2 = (uint8_t)(c2 >> 16),
    g2 = (uint8_t)(c2 >>  8),
    b2 = (uint8_t)c2;

    uint16_t
    r = r1 + r2,
    g = g1 + g2,
    b = b1 + b2;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    /*
    uint8_t
    r = (uint8_t)((r1 + r2) >> 1),
    g = (uint8_t)((g1 + g2) >> 1),
    b = (uint8_t)((b1 + b2) >> 1);
    */

    return strip.Color((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

uint32_t fadeColor(uint32_t c, uint16_t proportion, uint16_t divisor) {
    uint8_t
    r = (uint8_t)(((c >> 16) & 0xFF) * proportion / divisor),
    g = (uint8_t)(((c >> 8) & 0xFF) * proportion / divisor),
    b = (uint8_t)((c & 0xFF) * proportion / divisor);
    return strip.Color(r,g,b);
}

/*
uint32_t mixColors(uint32_t c1, uint32_t c2, uint32_t c3) {
uint32_t part = mixColors(c1, c2);
return mixColors(part, c3);
}
*/

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets


time_t getNtpTime()
{
    while (Udp.parsePacket() > 0) ; // discard any previously received packets
    //get a random server from the pool
    WiFi.hostByName(ntpServerName, timeServerIP);
    Serial.println("Transmit NTP Request");
    sendNTPpacket(timeServerIP);
    uint32_t beginWait = millis();
    while (millis() - beginWait < 1500) {
        int size = Udp.parsePacket();
        if (size >= NTP_PACKET_SIZE) {
            Serial.println("Receive NTP Response");
            Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
            unsigned long secsSince1900;
            // convert four bytes starting at location 40 to a long integer
            secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
            secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
            secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
            secsSince1900 |= (unsigned long)packetBuffer[43];
            ntp_fail = false;
            return secsSince1900 - 2208988800UL;
        }
    }
    ntp_fail = true;
    return 0; // return 0 if unable to get the time
}


// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
    // set all bytes in the buffer to 0
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    // Initialize values needed to form NTP request
    // (see URL above for details on the packets)
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12]  = 49;
    packetBuffer[13]  = 0x4E;
    packetBuffer[14]  = 49;
    packetBuffer[15]  = 52;
    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    Udp.beginPacket(address, 123); //NTP requests are to port 123
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();
}


void setup() {
    // Use Serial for debugging
    Serial.begin(115200);
    Serial.println("Booting");

    // Setup ADC Input pin
    pinMode(A0, INPUT);
    // Neopixels Setup
    strip.begin();
    ambientLight();
    // Startup Sequence
    walk(strip.Color(255,0,150), 160);
    strip.clear();
    // Show wifi status
    showStatus();
    strip.show();
    // Wifi start
    WiFiManager wifiManager;
    wifiManager.setDebugOutput(false);
    wifiManager.autoConnect("VB-CLOCK", "vb-clock");

    // Initialise NTP stuff
    Udp.begin(localPort);
    // This regularly checks NTP to update the time
    // Set the syn interval to 5 seconds until we manage to set the time
    setSyncInterval(60);
    setSyncProvider(getNtpTime);
    while(timeStatus() == timeNotSet){
        showStatus();
        delay(disp_update_period);
    }
    last_updated = millis();
    // Set the sync interval to something more reasonable
    setSyncInterval(ntp_sync_interval);

    // Include some sueful debugging feedback for the OTA programming
    ArduinoOTA.onStart([]() {
        Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    // Kick-off the OTA update code
    ArduinoOTA.setHostname("vb-clock");
    //ArduinoOTA.setPassword((const char *)"vb-clock");
    ArduinoOTA.begin();
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void loop() {
    time_now = millis();
    if(last_updated + disp_update_period < time_now) {
        last_updated = time_now;
        updateDisplay();
    }
    ArduinoOTA.handle();
    yield();
}
