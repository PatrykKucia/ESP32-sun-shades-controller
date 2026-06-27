#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>     // Biblioteka do pamiętania pozycji rolety (0-100%)
#include "SinricPro.h"
#include "SinricProBlinds.h"
#include "secrets.h"         // Twój plik z WIFI_SSID i WIFI_PASSWORD

// --- KONFIGURACJA SINRIC PRO ---
#define BAUD_RATE         115200                

// --- DEFINICJA PINÓW (TYLKO GÓRA, DÓŁ, OK) ---
const int PIN_OK    = 4;
const int PIN_GORA  = 3;
const int PIN_DOL   = 0;

// --- USTAWIENIA CZASOWE ---
const unsigned long CZAS_PELNEGO_OTWARCIA_MS = 20000; 
const int CZAS_IMPULSU_MS = 500; 

// --- PAMIĘĆ I STAN ---
Preferences pamiec;

// Zmienne globalne rolety
int aktualnaPozycja = 100; // 100 = otwarta, 0 = zamknięta
bool wRuchu = false;
int kierunek = 0;          // 1 = góra, -1 = dół
unsigned long czasRozpoczeciaRuchu = 0;
unsigned long czasDoZatrzymania = 0;

// ====================================================================
// LOGIKA PILOTA I PAMIĘCI
// ====================================================================

void kliknijPrzycisk(int pin) {
  digitalWrite(pin, HIGH);
  delay(CZAS_IMPULSU_MS);
  digitalWrite(pin, LOW);
}

// Zapis pozycji (procentowej) po jej osiągnięciu, żeby przetrwała reset układu
void zapiszPozycjeRolety() {
  pamiec.putInt("pozycja", aktualnaPozycja);
  Serial.printf("Zapisano pozycje %d%% do pamieci Flash.\r\n", aktualnaPozycja);
}

void obslugaRuchuRolety() {
  if (wRuchu) {
    if (millis() - czasRozpoczeciaRuchu >= czasDoZatrzymania) {
      
      // Zatrzymujemy tylko pozycje pośrednie (nie 0%, nie 100%)
      if (aktualnaPozycja != 0 && aktualnaPozycja != 100) {
        Serial.println("Osiagnieto pozycje posrednia. Wciskam STOP (OK).");
        kliknijPrzycisk(PIN_OK);                
      }
      
      wRuchu = false;
      zapiszPozycjeRolety(); // Zapisujemy nową pozycję na twardo
    }
  }
}

// ====================================================================
// CALLBACKI SINRIC PRO
// ====================================================================

// 1. Zmiana pozycji z suwaka (Google Home / Sinric App)
bool onRangeValue(const String &deviceId, int &pozycjaDocelowa) {
  if (pozycjaDocelowa == aktualnaPozycja) return true;

  Serial.printf("Zmiana pozycji rolet z %d na %d\r\n", aktualnaPozycja, pozycjaDocelowa);
  
  if (pozycjaDocelowa > aktualnaPozycja) {
    kliknijPrzycisk(PIN_GORA);
    kierunek = 1;
  } else {
    kliknijPrzycisk(PIN_DOL);
    kierunek = -1;
  }

  // Obliczenie czasu jazdy
  int roznica = abs(pozycjaDocelowa - aktualnaPozycja);
  czasDoZatrzymania = (CZAS_PELNEGO_OTWARCIA_MS * roznica) / 100;
  
  // Margines dla pozycji skrajnych, by krańcówka sama się rozłączyła
  if (pozycjaDocelowa == 0 || pozycjaDocelowa == 100) {
    czasDoZatrzymania += 2000; 
  }

  czasRozpoczeciaRuchu = millis();
  wRuchu = true;
  aktualnaPozycja = pozycjaDocelowa;
  
  return true;
}

// Względna zmiana pozycji (np. "podnieś o 10%")
bool onAdjustRangeValue(const String &deviceId, int &positionDelta) {
  int nowaPozycja = aktualnaPozycja + positionDelta;
  if (nowaPozycja > 100) nowaPozycja = 100;
  if (nowaPozycja < 0) nowaPozycja = 0;
  
  positionDelta = nowaPozycja; 
  return onRangeValue(deviceId, nowaPozycja);
}

// 2. Przycisk ON/OFF działa jako natychmiastowy STOP / Obliczenie pozycji w locie
bool onPowerState(const String &deviceId, bool &state) {
  Serial.println("Wymuszono STOP dla rolet");
  kliknijPrzycisk(PIN_OK);

  // Jeśli roleta właśnie jechała, obliczamy gdzie zatrzymaliśmy ją w połowie drogi
  if (wRuchu) {
    unsigned long mineloCzasu = millis() - czasRozpoczeciaRuchu;
    int procentPrzejechany = (mineloCzasu * 100) / CZAS_PELNEGO_OTWARCIA_MS;
    
    if (kierunek == 1) { // Jechała w górę
      aktualnaPozycja = aktualnaPozycja - abs(100 - aktualnaPozycja) + procentPrzejechany;
    } else { // Jechała w dół
      aktualnaPozycja = aktualnaPozycja + abs(aktualnaPozycja) - procentPrzejechany;
    }
    
    if (aktualnaPozycja > 100) aktualnaPozycja = 100;
    if (aktualnaPozycja < 0) aktualnaPozycja = 0;
    
    wRuchu = false;
    zapiszPozycjeRolety();
    Serial.printf("Pozycja po recznym zatrzymaniu: %d%%\r\n", aktualnaPozycja);
  }

  return true; 
}

// ====================================================================
// SETUP & LOOP
// ====================================================================

void setupWiFi() {
  Serial.printf("\r\n[Wifi]: Ladowanie");
  WiFi.setSleep(false); 
  WiFi.setAutoReconnect(true);

  // Wymuszenie publicznych, niezawodnych serwerów DNS (Google i Cloudflare)
  // Parametry: IP, Bramka, Maska, DNS 1, DNS 2
  // Zostawiając pierwsze trzy jako INADDR_NONE (lub 0U), IP nadal jest przydzielane automatycznie!
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.printf(".");
    delay(250);
  }
  Serial.printf(" polaczono!\r\n[WiFi]: IP to %s\r\n", WiFi.localIP().toString().c_str());
}

void setupSinricPro() {
  SinricProBlinds &myBlinds = SinricPro[BLINDS_ID];
  myBlinds.onPowerState(onPowerState);
  myBlinds.onRangeValue(onRangeValue);
  myBlinds.onAdjustRangeValue(onAdjustRangeValue);

  SinricPro.onConnected([](){ Serial.printf("Polaczono z SinricPro\r\n"); }); 
  SinricPro.onDisconnected([](){ Serial.printf("Rozlaczono z SinricPro\r\n"); });
  SinricPro.begin(APP_KEY, APP_SECRET);
}

void setup() {
  Serial.begin(BAUD_RATE); 
  Serial.printf("\r\n\r\n");

  // Inicjalizacja pamięci Flash (odczytujemy, na ilu procentach staneły rolety przed brakiem prądu)
  pamiec.begin("rolety", false);
  aktualnaPozycja = pamiec.getInt("pozycja", 100); 
  Serial.printf("Wczytano pamiec: Pozycja startowa to %d%%\r\n", aktualnaPozycja);

  // Inicjalizacja pinów dla transoptorów
  pinMode(PIN_OK, OUTPUT);
  pinMode(PIN_GORA, OUTPUT);
  pinMode(PIN_DOL, OUTPUT);
  
  digitalWrite(PIN_OK, LOW);
  digitalWrite(PIN_GORA, LOW);
  digitalWrite(PIN_DOL, LOW);

  setupWiFi();
  setupSinricPro();
}

void loop() {
  SinricPro.handle();
  obslugaRuchuRolety(); 
}
//////////////////////////////////////////// kod testowy do pilota webowego (nie używany w finalnej wersji) ////////////////////////////////////////////



// #include <WiFi.h>
// #include <WebServer.h>
// #include "secrets.h" // Twój plik z WIFI_SSID i WIFI_PASSWORD

// WebServer server(80);

// // Definicja pinów
// const int PIN_OK    = 4;
// const int PIN_GORA  = 3;
// const int PIN_DOL   = 0; // Pamiętaj o uwadze sprzętowej (strapping pin)!
// const int PIN_LEWO  = 1;
// const int PIN_PRAWO = 5;

// const int CZAS_IMPULSU_MS = 500; 

// // HTML i CSS (Wygląd pilota - układ krzyżaka)
// void handleRoot() {
//   String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
//   html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
//   html += "<title>Pilot Rolet</title>";
//   html += "<style>";
//   html += "body { font-family: Arial, sans-serif; background-color: #2c3e50; color: white; display: flex; flex-direction: column; align-items: center; margin-top: 50px; }";
//   html += "h2 { margin-bottom: 30px; }";
//   html += ".d-pad { display: flex; flex-direction: column; align-items: center; gap: 15px; }";
//   html += ".row { display: flex; gap: 15px; justify-content: center; }";
//   html += ".btn { width: 80px; height: 80px; border-radius: 15px; background: #34495e; color: white; text-decoration: none; display: flex; align-items: center; justify-content: center; font-size: 18px; font-weight: bold; box-shadow: 0 6px #1a252f; transition: all 0.1s; }";
//   html += ".btn:active { box-shadow: 0 2px #1a252f; transform: translateY(4px); }";
//   html += ".btn-ok { background: #e74c3c; box-shadow: 0 6px #c0392b; }";
//   html += ".btn-ok:active { box-shadow: 0 2px #c0392b; }";
//   html += "</style></head><body>";
  
//   html += "<h2>Pilot Rolet</h2>";
//   html += "<div class='d-pad'>";
  
//   // Górny wiersz
//   html += "<div class='row'><a href='/gora' class='btn'>GÓRA</a></div>";
//   // Środkowy wiersz
//   html += "<div class='row'>";
//   html += "<a href='/lewo' class='btn'>LEWO</a>";
//   html += "<a href='/ok' class='btn btn-ok'>OK</a>";
//   html += "<a href='/prawo' class='btn'>PRAWO</a>";
//   html += "</div>";
//   // Dolny wiersz
//   html += "<div class='row'><a href='/dol' class='btn'>DÓŁ</a></div>";
  
//   html += "</div></body></html>";
//   server.send(200, "text/html", html);
// }

// // Funkcja pomocnicza do "klikania" pinu
// void clickPin(int pin, const char* actionName) {
//   Serial.print("Wysyłam impuls: ");
//   Serial.println(actionName);
  
//   digitalWrite(pin, HIGH);
//   delay(CZAS_IMPULSU_MS);
//   digitalWrite(pin, LOW);
  
//   // Natychmiastowy powrót na stronę główną bez przeładowywania wizualnego
//   server.sendHeader("Location", "/");
//   server.send(303);
// }

// void setup() {
//   Serial.begin(115200);
  
//   // Konfiguracja pinów
//   int pins[] = {PIN_OK, PIN_GORA, PIN_DOL, PIN_LEWO, PIN_PRAWO};
//   for(int i=0; i<5; i++) {
//     pinMode(pins[i], OUTPUT);
//     digitalWrite(pins[i], LOW);
//   }

//   Serial.println("Łączenie z Wi-Fi...");
//   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(500);
//     Serial.print(".");
//   }
  
//   Serial.println("\nPołączono! IP: ");
//   Serial.println(WiFi.localIP());

//   // Rejestrowanie ścieżek URL przy użyciu lambd (skraca kod)
//   server.on("/", handleRoot);
//   server.on("/gora",  []() { clickPin(PIN_GORA, "GÓRA"); });
//   server.on("/dol",   []() { clickPin(PIN_DOL, "DÓŁ"); });
//   server.on("/lewo",  []() { clickPin(PIN_LEWO, "LEWO"); });
//   server.on("/prawo", []() { clickPin(PIN_PRAWO, "PRAWO"); });
//   server.on("/ok",    []() { clickPin(PIN_OK, "OK"); });
  
//   server.begin();
// }

// void loop() {
//   server.handleClient();
// }