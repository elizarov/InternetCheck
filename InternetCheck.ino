#include <Timeout.h>

#include <SPI.h>
#include <Ethernet.h>

#include "xprint.h"

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

const uint8_t POWER_PIN = 3;
const long REBOOT_TIME = 5000; // 5 sec -- with power off
const long BOOT_WAIT_TIME = 10000; // 10 sec -- after during power on

const long DHCP_TIMEOUT = 30000; // 30 sec 
const uint8_t DHCP_RETRIES = 4;

char server[] = "apps.vsegda.org";
EthernetClient client;

const long REBOOT_GRACE_TIME = 60000; // 1 min

const long CONNECTION_CHECK_TIME = 20000; // check every 20 sec
Timeout connectionTimeout(REBOOT_GRACE_TIME); // first check -- after reboot grace time

const uint8_t CONNECT_FAIL_REBOOT = 2; // raboot after two failures
uint8_t connectFail = 0;

const long READING_TIME = 20000; // 20 sec
Timeout readingTimeout; 

char expect[] = "Welcom"; // no repeat chars for simplicity
uint8_t expectIndex = 0;
bool expectFound = false;

long connectStartTime;

void setup() {
  // configure power pin (on)
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN, 0);
  // serial
  setupPrint();
  // DHCP
  uint8_t retry = 0;
  while (true) {
    waitPrint();
    Serial.print("{I:DHCP ");
    Serial.print(retry);
    Serial.println("}");
    if (Ethernet.begin(mac, DHCP_TIMEOUT)) {
      break;
    }
    retry++;
    if (retry >= DHCP_RETRIES) { 
      retry = 0;
      reboot();
    }
  }  
  displayIP();
}

void reboot() {
  waitPrintln("{I:reboot}*");
  digitalWrite(POWER_PIN, 1);
  delay(REBOOT_TIME);
  digitalWrite(POWER_PIN, 0);  
  connectionTimeout.reset(REBOOT_GRACE_TIME);
  delay(BOOT_WAIT_TIME);
}

void displayIP() {
  waitPrint();   
  Serial.print("{I:address ");
  Serial.print(Ethernet.localIP());
  Serial.println("}");
}

void maintainDHCP() {
  switch (Ethernet.maintain()) {
    case 1: 
      waitPrintln("{I:DHCP renew failed}*");
      break;
    case 2:
      waitPrintln("{I:DHCP renew success}");
      displayIP();
      break;
    case 3:
      waitPrintln("{I:DHCP rebind failed}*");
      break;
    case 4:
      waitPrintln("{I:DHCP rebind success}");
      displayIP();
      break;
  }  
}

void checkConnection() {
  if (readingTimeout.enabled()) return; // reading from connectoin now
  if (!connectionTimeout.check()) return; // not time to reconnect yet
  connectionTimeout.reset(CONNECTION_CHECK_TIME);
  waitPrint();
  Serial.print("{I:Connect ");
  Serial.print(server);
  Serial.println("}");
  connectStartTime = millis();
  if (!client.connect(server, 80)) { 
     waitPrintln("[I:0 e1]");
     handleFailure();
     return;
  }
  // Make a HTTP request:
  client.println("GET / HTTP/1.1");
  client.print("Host: "); client.println(server);
  client.println("Connection: close");
  client.println();
  expectFound = false;
  readingTimeout.reset(READING_TIME);
}

void handleFailure() {
   connectFail++;
   if (connectFail >= CONNECT_FAIL_REBOOT) {
      connectFail = 0;
      reboot();
   }  
}

void checkClientData() {
  if (!readingTimeout.enabled()) return; // not reading 
  if (readingTimeout.check()) {
    // read timed out
    waitPrintln("[I:0 e2}");
    client.stop();
    handleFailure();
    return;
  }
  if (client.available()) {
    char c = client.read();
    if (c == expect[expectIndex]) {
      expectIndex++;
      if (expect[expectIndex] == 0) {
        expectFound = true;
        expectIndex = 0;
      }
    } else {
      expectIndex = 0;
    }
  }
  if (!client.connected()) {
    client.stop();
    readingTimeout.disable();
    if (!expectFound) {
      waitPrintln("[I:0 e3]");
      handleFailure();
    } else {
      long delay = millis() - connectStartTime;
      waitPrint();
      Serial.print("[I:1 e0 d");
      Serial.print(delay);
      Serial.println("]");
      connectFail = 0;
    }
  }
}

void loop() {
  maintainDHCP();
  checkConnection();
  checkClientData();  
}

