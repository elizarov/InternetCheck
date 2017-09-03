#include <Timeout.h>

#include <SPI.h>
#include <EthernetX.h>

#include "xprint.h"

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

const uint8_t LED_PIN = 13; // for boot indication
const uint8_t POWER_PIN = 3;
const uint8_t W5100_RESET_PIN = A0;

const uint8_t BOOT_WAIT = 3; // wait & blink 3 sec on boot

const long REBOOT_TIME = 3000; // 3 sec -- with power off

const long DHCP_TIMEOUT = 30000; // 30 sec 
const uint8_t DHCP_RETRIES = 4;

bool initialized = false;

char server[] = "apps.vsegda.org";
EthernetClient client;

const long REBOOT_GRACE_TIME = 40000; // 40 sec

const long CONNECTION_CHECK_TIME = 20000; // check every 20 sec
Timeout connectionTimeout(REBOOT_GRACE_TIME); // first check -- after reboot grace time

const uint8_t CONNECT_FAIL_REBOOT = 2; // raboot after two failures
uint8_t connectFail = 0;

const long READING_TIME = 20000; // 20 sec
Timeout readingTimeout; 

char expect[] = "Welcom"; // no repeat chars for simplicity
uint8_t expectIndex = 0;
bool expectFound = false;

const long STATUS_TIME = 50000; // 50s -- don't send status too often
Timeout statusTimeout(0);

long connectStartTime;

void setup() {
  pinMode(W5100_RESET_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(POWER_PIN, OUTPUT);
  // configure power pin (on)
  digitalWrite(POWER_PIN, 0);
  // serial
  setupPrint();
  waitPrintln("{I:InternetCheck}");
}

void checkInit() {
  if (initialized) return;
  // boot blink sequence -- 3 blinks in 3 secs
  digitalWrite(W5100_RESET_PIN, 0);
  for (uint8_t i = 0; i < BOOT_WAIT; i++) {
    digitalWrite(LED_PIN, 1);
    delay(200);
    digitalWrite(LED_PIN, 0);
    delay(800);
  }
  digitalWrite(W5100_RESET_PIN, 1);
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
      return; // leave !initialized
    } else {
      delay(1000); 
    }
  }  
  displayIP();
  initialized = true;  
}

void reboot() {
  waitPrintln("{I:reboot}*");
  digitalWrite(POWER_PIN, 1);
  delay(REBOOT_TIME);
  digitalWrite(POWER_PIN, 0);  
  connectionTimeout.reset(REBOOT_GRACE_TIME);
  initialized = false; // force W5100 init in next loop
}

void displayIP() {
  waitPrint();   
  Serial.print("{I:address ");
  Serial.print(Ethernet.localIP());
  Serial.println("}");
}

void maintainDHCP() {
  if (!initialized) return;
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
  if (!initialized) return;
  if (readingTimeout.enabled()) return; // reading from connectoin now
  if (!connectionTimeout.check()) return; // not time to reconnect yet
  connectionTimeout.reset(CONNECTION_CHECK_TIME);
  waitPrint();
  Serial.print("{I:Connect ");
  Serial.print(server);
  Serial.println("}");
  connectStartTime = millis();
  if (!client.connect(server, 80)) { 
     reportError(1, "no connection");
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

bool criticalFailure() {
  return connectFail >= CONNECT_FAIL_REBOOT - 1;
}

void handleFailure() {
  if (criticalFailure()) {
      connectFail = 0;
      reboot();    
  } else {
    connectFail++;   
  }  
}

void reportError(int code, char* msg) {
  waitPrint();
  Serial.print("[I:0 e");
  Serial.print(code);
  Serial.print("]");
  Serial.print(msg);
  if (criticalFailure()) Serial.print("*");
  Serial.println();
}

void checkClientData() {
  if (!initialized) return;
  if (!readingTimeout.enabled()) return; // not reading 
  if (readingTimeout.check()) {
    reportError(2, "timeout");
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
      reportError(3, "no data");
      handleFailure();
    } else {
      long delay = millis() - connectStartTime;
      if (statusTimeout.check()) {
        waitPrint();
        Serial.print("[I:1 e0 d");
        Serial.print(delay);
        Serial.println("]");
        statusTimeout.reset(STATUS_TIME);
      }
      connectFail = 0;
    }
  }
}

void loop() {
  checkInit();
  maintainDHCP();
  checkConnection();
  checkClientData();  
}

