#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>     
#include "SinricPro.h"
#include "SinricProBlinds.h"
#include "secrets.h"      
#include "esp_system.h"   

#define BAUD_RATE         9600                

// --- DEFINICJA PINÓW ---
const int PIN_OK    = 4;
const int PIN_GORA  = 3;
const int PIN_DOL   = 0;

// --- USTAWIENIA CZASOWE ---
const unsigned long CZAS_PELNEGO_OTWARCIA_MS = 23000; 
const int CZAS_IMPULSU_MS = 500; 

// --- ZMIENNE DO WATCHDOGA DNS ---
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 20000; // Sprawdzaj co 20 sekund
int dnsFailCount = 0; // Licznik błędów DNS

// --- PAMIĘĆ I STAN ---
Preferences pamiec;

int aktualnaPozycja = 100; 
int pozycjaStartowa = 100; // Zmienna pomocnicza do precyzyjnego wyliczania STOP
bool wRuchu = false;
int kierunek = 0;          // 1 = góra, -1 = dół
unsigned long czasRozpoczeciaRuchu = 0;
unsigned long czasDoZatrzymania = 0;

// ====================================================================
// LOGIKA PILOTA
// ====================================================================

void kliknijPrzycisk(int pin) {
  Serial.printf("[Sprzet] Ustawiam PIN %d na HIGH na %d ms...\r\n", pin, CZAS_IMPULSU_MS);
  digitalWrite(pin, HIGH);
  delay(CZAS_IMPULSU_MS);
  digitalWrite(pin, LOW);
  Serial.printf("[Sprzet] PIN %d wrocial na LOW.\r\n", pin);
}

void zapiszPozycjeRolety() {
  pamiec.putInt("pozycja", aktualnaPozycja);
  Serial.printf("[Pamiec] Twardy zapis pozycji w pamieci Flash: %d%%\r\n", aktualnaPozycja);
}

void obslugaRuchuRolety() {
  if (wRuchu) {
    if (millis() - czasRozpoczeciaRuchu >= czasDoZatrzymania) {
      Serial.println("\r\n====================================");
      Serial.println("[Zegar] Czas jazdy minal!");
      
      if (aktualnaPozycja != 0 && aktualnaPozycja != 100) {
        Serial.println("[Decyzja] Pozycja posrednia. Wysylam fizyczny sygnal STOP (PIN_OK).");
        kliknijPrzycisk(PIN_OK);                
      } else {
        Serial.println("[Decyzja] Pozycja skrajna (0 lub 100). Nic nie klikam, krancowka w oknie sama wylaczy silnik.");
      }
      
      wRuchu = false;
      zapiszPozycjeRolety();
      Serial.println("====================================\r\n");
    }
  }
}

// ====================================================================
// CALLBACKI SINRIC PRO
// ====================================================================

bool onRangeValue(const String &deviceId, int &pozycjaDocelowa) {
  Serial.println("\r\n====================================");
  Serial.printf("[Sinric] Nowe zadanie: Zmien pozycje z %d%% na %d%%\r\n", aktualnaPozycja, pozycjaDocelowa);
  
  if (pozycjaDocelowa == aktualnaPozycja) {
    Serial.println("[Sinric] Roleta jest juz w tej pozycji. Ignoruje.");
    Serial.println("====================================\r\n");
    return true;
  }

  pozycjaStartowa = aktualnaPozycja; // Zapisujemy skąd ruszamy, na wypadek nagłego STOP
  
// LOGIKA KIERUNKU: 100% = Pełne OTWARCIE (Roleta na GÓRZE)
  if (pozycjaDocelowa > aktualnaPozycja) {
    Serial.println("[Silnik] Otwieram rolete (procenty rosna). Klikam PIN_GORA.");
    kliknijPrzycisk(PIN_GORA);
    kierunek = 1;
  } else {
    Serial.println("[Silnik] Zamykam rolete (procenty maleja). Klikam PIN_DOL.");
    kliknijPrzycisk(PIN_DOL);
    kierunek = -1;
  }
  int roznica = abs(pozycjaDocelowa - aktualnaPozycja);
  czasDoZatrzymania = (CZAS_PELNEGO_OTWARCIA_MS * roznica) / 100;
  Serial.printf("[Matematyka] Procent do przejechania: %d%%. Wyliczony czas: %lu ms\r\n", roznica, czasDoZatrzymania);
  
  if (pozycjaDocelowa == 0 || pozycjaDocelowa == 100) {
    czasDoZatrzymania += 2000; 
    Serial.println("[Silnik] Cel to krancowka. Dodano 2000 ms zapasu bezpieczenstwa.");
  }

  czasRozpoczeciaRuchu = millis();
  wRuchu = true;
  aktualnaPozycja = pozycjaDocelowa; // Ustawiamy stan dla aplikacji
  
  Serial.println("====================================\r\n");
  return true;
}

bool onAdjustRangeValue(const String &deviceId, int &positionDelta) {
  int nowaPozycja = aktualnaPozycja + positionDelta;
  if (nowaPozycja > 100) nowaPozycja = 100;
  if (nowaPozycja < 0) nowaPozycja = 0;
  
  positionDelta = nowaPozycja; 
  return onRangeValue(deviceId, nowaPozycja);
}

bool onPowerState(const String &deviceId, bool &state) {
  Serial.println("\r\n====================================");
  Serial.println("[Sinric] Wcisnieto Glowny Przycisk ON/OFF. Traktuje to jako komende STOP!");
  kliknijPrzycisk(PIN_OK);

  if (wRuchu) {
    unsigned long mineloCzasu = millis() - czasRozpoczeciaRuchu;
    int procentPrzejechany = (mineloCzasu * 100) / CZAS_PELNEGO_OTWARCIA_MS;
    
    Serial.printf("[Stop] Roleta jechala przez %lu ms, co daje ok. %d%%\r\n", mineloCzasu, procentPrzejechany);
    
    if (kierunek == 1) { // Jechała w górę
      aktualnaPozycja = pozycjaStartowa + procentPrzejechany;
    } else { // Jechała w dół
      aktualnaPozycja = pozycjaStartowa - procentPrzejechany;
    }
    
    if (aktualnaPozycja > 100) aktualnaPozycja = 100;
    if (aktualnaPozycja < 0) aktualnaPozycja = 0;
    
    wRuchu = false;
    zapiszPozycjeRolety();
    Serial.printf("[Stop] Skorygowano pozycje rolety. Obecnie znajduje sie na: %d%%\r\n", aktualnaPozycja);
  } else {
    Serial.println("[Stop] Roleta nie byla w ruchu. Wysłano tylko sygnał do pilota.");
  }

  Serial.println("====================================\r\n");
  return true; 
}

// ====================================================================
// SETUP & LOOP
// ====================================================================

void setupWiFi() {
  Serial.printf("\r\n[Wifi]: Ladowanie");
  WiFi.setSleep(false); 
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.printf(".");
    delay(250);
  }
  Serial.printf(" polaczono!\r\n[WiFi]: IP to %s\r\n", WiFi.localIP().toString().c_str());
  
  // --- TESTOWANIE DNS NA STARCIE ---
  IPAddress serwerIP;
  int probyDNS = 0;
  
  Serial.print("[WiFi]: Testowanie serwera DNS (ws.sinric.pro)...");
  
  // Próbujemy przetłumaczyć adres max 5 razy
  while(WiFi.hostByName("ws.sinric.pro", serwerIP) != 1 && probyDNS < 5) {
    Serial.print(" błąd! Ponawiam... ");
    delay(2000); // Czekamy 2 sekundy przed kolejną próbą
    probyDNS++;
  }

  // Decyzja na podstawie wyniku testu
  if (probyDNS >= 5) {
    Serial.println("\r\n[KRYTYCZNE]: DNS calkowicie nie dziala! Wymuszam restart ESP...");
    delay(1000);
    ESP.restart(); // Twardy restart procesora - często naprawia zawieszone moduły sieciowe
  } else {
    Serial.printf(" Sukces! IP serwera to: %s\r\n", serwerIP.toString().c_str());
  }
  // Usunięto zduplikowany blok oczekiwania na WiFi
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
  delay(1000); // Czekamy na USB
  Serial.printf("\r\n\r\n");
  WiFi.mode(WIFI_STA);

  // --- DIAGNOSTYKA RESTARTU ---
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.print("[DIAGNOSTYKA] Przyczyna ostatniego restartu: ");
  switch (reason) {
    case ESP_RST_POWERON: Serial.println("Zwykle podlaczenie do pradu"); break;
    case ESP_RST_PANIC:   Serial.println("PANIC / EXCEPTION - Powazny blad w kodzie lub pamieci!"); break;
    case ESP_RST_INT_WDT: Serial.println("Watchdog (Zaciecie procesora)"); break;
    case ESP_RST_BROWNOUT:Serial.println("BROWNOUT - Nagly spadek napiecia! (Zwarcie/Przeciazenie)"); break;
    case ESP_RST_SW:      Serial.println("Reset programowy"); break;
    default:              Serial.printf("Inna (Kod: %d)\n", reason);
  }
  Serial.println("------------------------------------");
  pamiec.begin("rolety", false);
  aktualnaPozycja = pamiec.getInt("pozycja", 0); 
  Serial.printf("[Start] Wczytano pamiec: Pozycja startowa to %d%%\r\n", aktualnaPozycja);

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
  // 1. Obsługa chmury i ruchu silnika
  SinricPro.handle();
  obslugaRuchuRolety(); 

  // 2. Łagodny Watchdog (bez agresywnego restartowania modułu WiFi)
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
    lastWiFiCheck = currentMillis; 

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi]: Brak polaczenia z routerem. Czekam na automatyczne wznowienie (AutoReconnect)...");
      // USUNIĘTO: WiFi.disconnect() i WiFi.reconnect()
      // Pozwalamy bibliotece ESP32 działać w tle.
    } else {
      // Jeśli mamy WiFi, robimy delikatny test DNS
      IPAddress testIP;
      if (WiFi.hostByName("ws.sinric.pro", testIP) == 1) {
        if (dnsFailCount > 0) {
          Serial.println("[Watchdog]: DNS dziala poprawnie.");
        }
        dnsFailCount = 0; 
      } else {
        dnsFailCount++;
        Serial.printf("[Watchdog]: OSTRZEZENIE! Blad DNS. Licznik: %d/5\r\n", dnsFailCount);
        
        // Zwiększyliśmy tolerancję do 5 błędów (ok. 100 sekund)
        if (dnsFailCount >= 5) {
          Serial.println("\r\n====================================");
          Serial.println("[KRYTYCZNE]: Krytyczny brak DNS. Wymuszam TWARDY RESTART...");
          Serial.println("====================================\r\n");
          delay(1000); 
          ESP.restart(); 
        }
      }
    }
  }
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
// const int PIN_PRAWO = 10;

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