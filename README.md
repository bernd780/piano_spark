# kawai_wled

**ESP32-S3 USB-MIDI-Bridge fuer ein Kawai Digitalpiano — reaktives LED-Ambientelicht + Home Assistant Integration.**

Ein **ESP32-S3** haengt als **USB-MIDI-Host** am Klavier, liest Noten, Pedale und Velocity und treibt damit:

- **WS2812B LED-Streifen** als Klaviatur-Licht (Anschlag → Flare, Tonhoehe → Farbe, Pedal → Nachgluehen)
- **MQTT/Home Assistant** Discovery (Sensoren, Schalter, Regler, Buttons — alles automatisch)
- **Spielzeit-Tracking** mit konfigurierbarem Tagesziel, Fortschrittsbalken auf den LEDs, Meilenstein-Sounds
- **Ruf-Melodien**: das Klavier erinnert per MIDI-Out ans Ueben (Zeitfenster + Haeufigkeit per HA-Schieber)
- **Aufnahme/Wiedergabe** eigener Stuecke + eingebaute Melodien (Clair de Lune, Nocturne, Entertainer, ...)
- **OTA-Updates** ueber WLAN

> Der Name „wled" ist historisch — die Firmware ist **eigenstaendig** und hat nichts mit Stock-WLED zu tun.
> Stock-WLED kann kein USB-MIDI; diese Firmware liest das Klavier direkt per USB-Host.

## Features

### LED-Effekte
| Modus | Beschreibung |
|---|---|
| **Bunt** (Default) | Tonhoehe → Farbe (Bass rot, Hoehen weiss), dezentes Regenbogen-Klimbim |
| **Classic** | Warmes Gold, eleganter |
| **Party** | Voller Regenbogen, schnell rotierend, optionale Funken |

- **Anschlagstaerke** → Helligkeit/Energie (klingt ab, Sustain-Pedal verlaengert)
- **Idle** → Knight-Rider-Lauflicht (Farbe zeigt Ziel-Status: bunt = noch offen, gruen = geschafft)
- **Standby** nach konfigurierbarer Zeit → LEDs aus, periodisches Aufwachen

### Spielzeit & Gamification
- **Tagesziel** in Minuten (HA-Schieber, Default 30 Min)
- **Fortschrittsbalken** dauerhaft dezent auf den LEDs (ein-/ausschaltbar per HA-Schalter)
- **Meilensteine** (alle 10%): Balken leuchtet auf, Coin-Sound bei 20/40/60/80%
- **Feier-Animation** (15 Sekunden Regenbogen → Gruen) + Fanfare bei 100%
- **Tagesreset um Mitternacht** (NTP) — Spielzeit ueberlebt Reboot/OTA am gleichen Tag

### Home Assistant
Automatische MQTT Discovery — das Klavier erscheint als Device mit:
- **Sensoren:** Letzte Note, Velocity, aktive Tasten, Spielzeit, Ziel-%, Toene heute
- **Binaer-Sensoren:** Verbunden, Spielt, Sustain/Sostenuto/Soft-Pedal, Tagesziel erreicht
- **Regler:** Max. Helligkeit, Klimbim, Lauflicht, Tagesziel, Aufforderungs-Zeitfenster/-Haeufigkeit, Funken, Audio-Erinnerung
- **Schalter:** Fortschrittsbalken an/aus
- **Dropdown:** Lichtmodus (Bunt/Classic/Party)
- **Buttons:** Melodien abspielen, Aufnahme starten/stoppen/abspielen, Test-Funktionen

## Hardware

| Teil | Empfehlung |
|---|---|
| Controller | **ESP32-S3 DevKitC** (USB-Host-faehig, OTG-Jumper gebrueckt) |
| LEDs | **WS2812B** (oder SK6812 RGBW), 60-81 LEDs |
| Netzteil | **5 V / 2 A** (FastLED begrenzt auf 1600 mA per Software) |
| USB | **USB-A-Buchse** → per A-B-Kabel ans Klavier |

### Pinbelegung (YD-ESP32-S3)
| Anschluss | GPIO |
|---|---|
| LED-Daten (WS2812B) | 21 |
| USB D- (zum Klavier) | 19 |
| USB D+ (zum Klavier) | 20 |

**Wichtig:** Der ESP32-S3 liefert **kein VBUS** an der nativen USB-Buchse. Die 5 V fuer das Klavier muessen **extern** eingespeist werden (vom Netzteil auf den VBUS-Pin der USB-A-Buchse).

## How To

### Voraussetzungen
- [PlatformIO](https://platformio.org/) (CLI oder VS Code Extension)
- ESP32-S3 Board mit USB-OTG
- WLAN-Zugang
- MQTT-Broker (z.B. Mosquitto in Home Assistant)

### 1. Repository klonen
```bash
git clone https://github.com/bernd780/kawai_wled.git
cd kawai_wled
```

### 2. Secrets anlegen
Erstelle `src/secrets.h` (wird von Git ignoriert):
```cpp
#define WIFI_SSID "DeinWLAN"
#define WIFI_PASS "DeinPasswort"
#define MQTT_HOST "192.168.x.x"
#define MQTT_PORT 1883
#define MQTT_USER "mqtt_user"
#define MQTT_PASS "mqtt_pass"
```

### 3. Konfiguration anpassen
In `src/main.cpp` oben im Konfig-Block bei Bedarf aendern:
```cpp
#define LED_PIN      21        // GPIO fuer LED-Daten
#define NUM_LEDS     81        // Anzahl LEDs am Streifen
#define LED_REVERSED 0         // 1 wenn Bass/Hoehe vertauscht
#define MAX_BRIGHT   210       // Max-Helligkeit (0-255)
#define AMBIENT_MAX  34        // Klimbim-Grundhelligkeit
```

### 4. Erster Upload per USB
```bash
pio run -t upload
```
Der ESP32-S3 muss beim ersten Mal per USB-Kabel am PC haengen (nicht am Klavier).

### 5. Folgende Updates per OTA
Nach dem ersten Flash ist OTA aktiv. Der ESP meldet sich als `kawai-klavier.local`:
```bash
pio run -t upload --upload-port kawai-klavier.local
```

### 6. Home Assistant
Nach dem Boot verbindet sich der ESP mit WLAN und MQTT. In Home Assistant erscheint automatisch ein neues Device **"Kawai Klavier"** mit allen Sensoren, Reglern und Buttons. Keine manuelle Konfiguration noetig.

### 7. Hardware zusammenbauen
1. **LED-Streifen** an GPIO 21 + GND + 5V vom Netzteil
2. **USB-A-Buchse** an GPIO 19 (D-), GPIO 20 (D+), GND, und **5V vom Netzteil auf VBUS**
3. **USB-A-auf-B-Kabel** von der Buchse zum "To Host"-Port am Klavier
4. ESP32 per Netzteil (oder USB) mit Strom versorgen

### Troubleshooting
- **Klavier wird nicht erkannt:** VBUS (5V) an der USB-A-Buchse pruefen. Ohne externe 5V erkennt das Klavier keinen Host.
- **LEDs falsch herum:** `LED_REVERSED` auf 1 setzen.
- **MQTT verbindet nicht:** Serial Monitor (115200 Baud) pruefen — WLAN-Status und MQTT-Verbindung werden geloggt.
- **OTA schlaegt fehl:** ESP muss im gleichen Netz sein. `ping kawai-klavier.local` testen.
- **Spielzeit wird beim Reboot zurueckgesetzt:** NTP muss erreichbar sein — ohne Uhrzeit kann der Tagesreset nicht funktionieren.

## Abhaengigkeiten
- [ESP32_Host_MIDI](https://github.com/sauloverissimo/ESP32_Host_MIDI) — USB-MIDI-Host
- [PubSubClient](https://github.com/knolleary/pubsubclient) — MQTT
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) v7 — JSON fuer HA Discovery
- [FastLED](https://github.com/FastLED/FastLED) — LED-Ansteuerung

## Status
Produktiv im Einsatz mit einem **Kawai CA63** (USB-MIDI, Kanal 1). Getestet mit YD-ESP32-S3 + 81 WS2812B LEDs.

## Lizenz
MIT
