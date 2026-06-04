#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <Updater.h>
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

const uint8_t DOT_BIT = 0x01; // nejnižší bit = tečka / decimal point
bool digitDot[8] = { false, false, false, false, false, false, false, false };

uint8_t cisliceProCislo(int cislo, bool tecka = false) {
  uint8_t bits = 0x00;
  if (cislo >= 0 && cislo <= 9) {
    bits = cislice[cislo];
  }
  if (tecka) {
    bits |= DOT_BIT;
  }
  return bits;
}

void setDigitDot(int index, bool enable) {
  if (index < 0 || index >= 8) return;
  digitDot[index] = enable;
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

  int celkemSekund = a / 1000;
  int minuty = celkemSekund / 60;
  int sekundy = celkemSekund % 60;

  pole[0] = cisliceProCislo((a / 10) % 10, digitDot[0]);
  pole[1] = cisliceProCislo((a / 100) % 10, digitDot[1]);
  pole[2] = cisliceProCislo(sekundy % 10, digitDot[2]); //vteřiny
  pole[3] = cisliceProCislo(sekundy / 10, digitDot[3]); //desítky vteřin

  if ((minuty % 10) <= 0) {
    pole[4] = cisliceProCislo(-1, digitDot[4]);
  } else {
    pole[4] = cisliceProCislo(minuty % 10, digitDot[4]); //minuty
  }

  if (((minuty / 10) % 10) <= 0) {
    pole[5] = cisliceProCislo(-1, digitDot[5]);
  } else {
    pole[5] = cisliceProCislo((minuty / 10) % 10, digitDot[5]); //desitky minut
  }
  
  pole[6] = cisliceProCislo(-1, digitDot[6]); //zatim vypnuto
  pole[7] = cisliceProCislo(-1, digitDot[7]); // rezervní pozice

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

  server.on("/update", HTTP_GET, []() {
    const char* page = "<!DOCTYPE html><html><body>"
                       "<h1>Firmware upload</h1>"
                       "<form method='POST' action='/update' enctype='multipart/form-data'>"
                       "<input type='file' name='update'><br><br>"
                       "<input type='submit' value='Upload firmware'>"
                       "</form></body></html>";
    server.send(200, "text/html", page);
  });

  server.on("/update", HTTP_POST,
    []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
      ESP.restart();
    },
    []() {
      HTTPUpload& upload = server.upload();

      if (upload.status == UPLOAD_FILE_START) {
        Serial.println("Start firmware upload");
        Update.begin(upload.totalSize);
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (!Update.end(true)) {
          Update.printError(Serial);
        }
        Serial.println("Upload finished");
      }
    }
  );

  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
  server.begin();
  Serial.println("HTTP server started on /data and /update");


  // ArduinoOTA (standardní OTA)
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
  Serial.println("OTA ready");
}

void loop() {
  server.handleClient();
  
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