#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <ElegantOTA.h>
#include "secret.h"

// ============================================
// FEATURE TOGGLES - Zapínání/vypínání funkcí
// ============================================
#define ENABLE_MSB_FEATURE    false    // MSB funkce
#define ENABLE_OTA_UPDATES    true    // elegantOTA
#define ENABLE_DEBUG_LOGGING  true    // Debug výstupy

// Tvoje stávající zapojení
#define DS_PIN    D4  // Serial Data
#define SHCP_PIN  D1  // Clock (SRCK)
#define STCP_PIN  D0  // Latch (RCK)

ESP8266WebServer server(80);
bool debug_mode = true;
int casA = 0;
int casB = 0;
int lastCasA = -1;
int lastCasB = -1;

// Tabulka pro čísla 0-9 (segmenty Q1 až Q7)
// Formát: 0bGFEDCBAx (x je Q0, ten v tabulce necháváme na 0)
const uint8_t cislice[] = {
  0b01110111, // 0
  0b00010100, // 1
  0b00111011, // 2
  0b00111110, // 3
  0b01011100, // 4
  0b01101110, // 5
  0b01101111, // 6
  0b00110100, // 7
  0b01111111, // 8
  0b01111110  // 9
};

uint8_t cisliceProCislo(int cislo) {
  if (cislo < 0 || cislo > 9) {
    return 0x00;
  }
  return cislice[cislo];
}

// Soft SPI transfer - optimized for speed
void swSPITransfer(uint8_t hodnota) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(DS_PIN, (hodnota & (1 << i)) ? HIGH : LOW);
    digitalWrite(SHCP_PIN, HIGH);
    digitalWrite(SHCP_PIN, LOW);
  }
}

void aktualizujRetezec(uint8_t *data) {
  digitalWrite(STCP_PIN, LOW);

  for (int i = 7; i >= 0; i--) {
    swSPITransfer(data[i]);
  }

  digitalWrite(STCP_PIN, HIGH);
}

void zobrazCasy(int a, int b) {
  uint8_t pole[8] = {0};

  int celkemSekund = casA / 1000;
  int minuty = celkemSekund / 60;
  int sekundy = celkemSekund % 60;

  pole[0] = cisliceProCislo((casA / 10) % 10);
  pole[1] = cisliceProCislo((casA / 100) % 10);
  pole[2] = cisliceProCislo(sekundy % 10); //vteriny
  pole[3] = cisliceProCislo(sekundy / 10); //desitky vterin

  pole[4] = cisliceProCislo(minuty % 10); //minuty
  pole[5] = cisliceProCislo((minuty / 10) % 10); //desitky minut
  pole[6] = cisliceProCislo(-1); //zatim vypnuto

  aktualizujRetezec(pole);
}

void updateDisplayIfChanged(int a, int b) {
  if (a == lastCasA && b == lastCasB) {
    return;
  }

  lastCasA = a;
  lastCasB = b;

  if (ENABLE_DEBUG_LOGGING) {
    Serial.print("Display update: ");
    Serial.print(a);
    Serial.print(" / ");
    Serial.println(b);
  }

  zobrazCasy(a, b);
}

void handleDataRequest() {
  String payload = "{\"time_a\":" + String(casA) + ",\"time_b\":" + String(casB) + "}";
  server.send(200, "application/json", payload);
}

// ============================================
// MSB funkce - Most Significant Bit feature
// ============================================
#if ENABLE_MSB_FEATURE
uint8_t extractMSB(uint8_t value) {
  // Vrátí nejvýznamnější bit
  return (value >> 7) & 1;
}

void processMSB() {
  // Logika pro zpracování MSB
  if (debug_mode) {
    Serial.print("MSB casA: ");
    Serial.println(extractMSB(casA));
  }
}
#endif

void setup() {
  pinMode(DS_PIN, OUTPUT);
  pinMode(SHCP_PIN, OUTPUT);
  pinMode(STCP_PIN, OUTPUT);

  digitalWrite(STCP_PIN, HIGH);
  digitalWrite(SHCP_PIN, LOW);
  digitalWrite(DS_PIN, LOW);

  Serial.begin(115200);
  WiFi.softAP(wifi_ssid, wifi_password);
  delay(100);
  Serial.println();
  Serial.println("ESP8266 AP mode active");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/data", HTTP_GET, handleDataRequest);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
  server.begin();
  Serial.println("HTTP server started on /data");

  // elegantOTA setup
  #if ENABLE_OTA_UPDATES
    ElegantOTA.begin(&server);
    Serial.println("elegantOTA ready - přístup na http://" + WiFi.softAPIP().toString() + "/update");
  #endif

  // ArduinoOTA (standardní OTA)
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
  Serial.println("OTA ready");
}

void loop() {
  server.handleClient();
  
  // elegantOTA handler
  #if ENABLE_OTA_UPDATES
    ElegantOTA.loop();
  #endif
  
  ArduinoOTA.handle();

  // MSB feature processing
  #if ENABLE_MSB_FEATURE
    processMSB();
  #endif

  static unsigned long lastUpdate = 0;
  if (debug_mode && millis() - lastUpdate >= 10) { // Aktualizace každých 1 ms
    lastUpdate = millis();
    casA = casA + 10; //tisiciny sekundy
    casB = casB + 10; //tisiciny sekundy
  }

  updateDisplayIfChanged(casA, casB);
}