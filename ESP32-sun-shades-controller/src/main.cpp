#include <WiFi.h>
#include <WebServer.h>
#include "secrets.h

// Uruchomienie serwera na porcie 80
WebServer server(80);

// Definicja pinów GPIO zgodnie z Twoim planem
const int PIN_GORA = 1;
const int PIN_DOL = 2;

// Czas trwania impulsu (w milisekundach) - udawanie kliknięcia fizycznego
const int CZAS_IMPULSU_MS = 500; 

// Funkcja generująca stronę główną HTML
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Sterowanie Roletami</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; text-align: center; background-color: #f4f4f9; color: #333; padding-top: 50px; }";
  html += "h1 { color: #444; }";
  html += ".btn { display: inline-block; width: 200px; padding: 20px; margin: 15px; font-size: 22px; font-weight: bold; color: white; text-decoration: none; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); transition: transform 0.1s; }";
  html += ".btn:active { transform: scale(0.95); }";
  html += ".btn-up { background-color: #2ecc71; }";
  html += ".btn-down { background-color: #3498db; }";
  html += "</style></head><body>";
  
  html += "<h1>Sterowanie Roletami ESP32-C3</h1>";
  html += "<p>Kliknij przycisk, aby wysłać impuls sterujący do pilota.</p>";
  html += "<a href='/gora' class='btn btn-up'>▲ GÓRA</a><br>";
  html += "<a href='/dol' class='btn btn-down'>▼ DÓŁ</a>";
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// Obsługa kliknięcia przycisku GÓRA
void handleGora() {
  Serial.println("Wysyłam impuls: GÓRA (GPIO 1)");
  digitalWrite(PIN_GORA, HIGH);   // Włącz sygnał
  delay(CZAS_IMPULSU_MS);         // Czekaj
  digitalWrite(PIN_GORA, LOW);    // Wyłącz sygnał
  
  // Przekieruj użytkownika z powrotem na stronę główną
  server.sendHeader("Location", "/");
  server.send(303);
}

// Obsługa kliknięcia przycisku DÓŁ
void handleDol() {
  Serial.println("Wysyłam impuls: DÓŁ (GPIO 2)");
  digitalWrite(PIN_DOL, HIGH);   // Włącz sygnał
  delay(CZAS_IMPULSU_MS);         // Czekaj
  digitalWrite(PIN_DOL, LOW);    // Wyłącz sygnał
  
  // Przekieruj użytkownika z powrotem na stronę główną
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  
  // Konfiguracja pinów jako wyjścia
  pinMode(PIN_GORA, OUTPUT);
  pinMode(PIN_DOL, OUTPUT);
  
  // Stan początkowy - niski (przyciski nie są wciśnięte)
  digitalWrite(PIN_GORA, LOW);
  digitalWrite(PIN_DOL, LOW);

  // Połączenie z siecią Wi-Fi
  Serial.print("Łączenie z siecią ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("Połączono z Wi-Fi!");
  Serial.print("Adres IP serwera: ");
  Serial.println(WiFi.localIP()); // Ten adres wpisujesz w przeglądarce smartfona

  // Definiowanie ścieżek URL dla serwera
  server.on("/", handleRoot);
  server.on("/gora", handleGora);
  server.on("/dol", handleDol);
  
  // Start serwera
  server.begin();
  Serial.println("Serwer WWW uruchomiony.");
}

void loop() {
  // Obsługa nadchodzących żądań klientów (smartfonów)
  server.handleClient();
}