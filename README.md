# piano_spark

**ESP32-S3 USB-MIDI bridge for digital pianos — reactive LED ambient lighting + practice gamification + Home Assistant integration.**

An **ESP32-S3** acts as a **USB-MIDI host** connected directly to the piano, reading notes, pedals and velocity to drive:

- **WS2812B LED strip** as keyboard light (velocity → flare, pitch → color, pedal → sustain glow)
- **MQTT / Home Assistant** auto-discovery (sensors, switches, sliders, buttons — zero config)
- **Practice tracking** with configurable daily goal, progress bar on the LEDs, milestone sounds
- **Call melodies**: the piano reminds you to practice via MIDI-out (time window + frequency adjustable in HA)
- **Record / playback** of your own performances + built-in pieces (Clair de Lune, Nocturne, Entertainer, ...)
- **OTA updates** over WiFi

## Features

### LED Effects
| Mode | Description |
|---|---|
| **Colorful** (default) | Pitch → color (bass = red, treble = white), subtle rainbow shimmer |
| **Classic** | Warm gold, elegant |
| **Party** | Full rainbow, fast rotation, optional sparks |

- **Velocity** → brightness / energy (decays over time, sustain pedal extends it)
- **Idle** → Knight Rider scanner (color indicates goal status: colorful = open, green = done)
- **Standby** after configurable timeout → LEDs off, periodic wake-up

### Practice & Gamification
- **Daily goal** in minutes (HA slider, default 30 min)
- **Progress bar** permanently visible on the LEDs (toggleable via HA switch)
- **Milestones** (every 10%): bar flashes bright, coin sound at 20/40/60/80%
- **Celebration animation** (15 seconds rainbow → green) + fanfare at 100%
- **Daily reset at midnight** (NTP) — practice time survives reboot/OTA on the same day

### Home Assistant
Automatic MQTT discovery — the piano appears as a device with:
- **Sensors:** last note, velocity, active keys, practice time, goal %, notes today
- **Binary sensors:** connected, playing, sustain/sostenuto/soft pedal, daily goal reached
- **Sliders:** max brightness, shimmer, scanner, daily goal, call window/frequency, sparks, audio reminder
- **Switch:** progress bar on/off
- **Dropdown:** light mode (Colorful/Classic/Party)
- **Buttons:** play melodies, start/stop/play recording, test functions

## Hardware

| Part | Recommendation |
|---|---|
| Controller | **YD-ESP32-S3** (DevKitC-1 clone with CH343 UART, USB-OTG solder jumper) |
| LEDs | **WS2812B** (or SK6812 RGBW), 60-81 LEDs |
| Power supply | **5 V / 2 A** (FastLED software-limits to 1600 mA) |
| USB cable | **USB-C to B cable** directly from the ESP to the piano's "To Host" port |

### Pin Assignment (YD-ESP32-S3)
| Connection | GPIO / Port |
|---|---|
| LED data (WS2812B) | GPIO 21 |
| USB to piano | **Native USB-C port** (D-=GPIO19, D+=GPIO20) |
| Flashing / serial monitor | **UART USB-C port** (CH343, COM port on PC) |

### USB-OTG Solder Jumper (mandatory!)

The native USB-C port on the ESP32-S3 ships as a **device port** and does **not supply VBUS**.
Without VBUS the piano won't detect a USB host — nothing happens.

On the **back side** of the YD-ESP32-S3 there is a small solder jumper labeled **"USB-OTG"**.
It must be **bridged with a drop of solder**. This connects the 5V rail (from the UART port or
power supply) to the VBUS pin of the native USB port.

> **Warning:** Do **NOT** touch the adjacent jumper labeled **"IN-OUT"** — it controls the power
> supply direction and can damage the board if set incorrectly.

After soldering, plug the **USB-C to B cable** directly into the native USB-C port and connect
it to the piano. No adapter or external USB-A socket needed.

## How To

### Prerequisites
- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- ESP32-S3 board with USB-OTG
- WiFi access
- MQTT broker (e.g. Mosquitto in Home Assistant)

### 1. Clone the repository
```bash
git clone https://github.com/bernd780/piano_spark.git
cd piano_spark
```

### 2. Create secrets file
Create `src/secrets.h` (git-ignored):
```cpp
#define WIFI_SSID "YourWiFi"
#define WIFI_PASS "YourPassword"
#define MQTT_HOST "192.168.x.x"
#define MQTT_PORT 1883
#define MQTT_USER "mqtt_user"
#define MQTT_PASS "mqtt_pass"
```

### 3. Adjust configuration
In `src/main.cpp` at the top, adjust as needed:
```cpp
#define LED_PIN      21        // GPIO for LED data
#define NUM_LEDS     81        // number of LEDs on the strip
#define LED_REVERSED 0         // 1 if bass/treble are swapped
#define MAX_BRIGHT   210       // max brightness (0-255)
#define AMBIENT_MAX  34        // background shimmer brightness
```

### 4. First upload via USB
```bash
pio run -t upload
```
The ESP32-S3 must be connected to the PC via USB for the first flash (not to the piano).

### 5. Subsequent updates via OTA
After the first flash, OTA is active. The ESP announces itself as `kawai-klavier.local`:
```bash
pio run -t upload --upload-port kawai-klavier.local
```

### 6. Home Assistant
After boot the ESP connects to WiFi and MQTT. A new device **"Kawai Klavier"** automatically
appears in Home Assistant with all sensors, sliders and buttons. No manual configuration needed.

### 7. Hardware assembly
1. **Solder the USB-OTG jumper** on the back of the YD-ESP32-S3 (see above)
2. **LED strip** to GPIO 21 + GND + 5V from power supply
3. **USB-C to B cable** from the native USB-C port to the piano's "To Host" port
4. Power the ESP via the UART port (laptop) or external power supply

### Troubleshooting
- **Piano not detected:** Check the USB-OTG solder jumper on the back — without it, the native port has no VBUS and the piano won't enumerate.
- **LEDs reversed:** Set `LED_REVERSED` to 1.
- **MQTT not connecting:** Check serial monitor (115200 baud) — WiFi status and MQTT connection are logged.
- **OTA fails:** ESP must be on the same network. Test with `ping kawai-klavier.local`.
- **Practice time resets on reboot:** NTP must be reachable — without a clock, the daily reset logic can't work.

## Dependencies
- [ESP32_Host_MIDI](https://github.com/sauloverissimo/ESP32_Host_MIDI) — USB MIDI host
- [PubSubClient](https://github.com/knolleary/pubsubclient) — MQTT
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) v7 — JSON for HA discovery
- [FastLED](https://github.com/FastLED/FastLED) — LED driver

## Status
In daily use with a **Kawai CA63** (USB-MIDI, channel 1). Tested with YD-ESP32-S3 + 81 WS2812B LEDs.

## License
MIT

---

# piano_spark (deutsch)

**ESP32-S3 USB-MIDI-Bridge fuer Digitalpianos — reaktives LED-Ambientelicht + Uebungs-Gamification + Home Assistant Integration.**

Ein **ESP32-S3** haengt als **USB-MIDI-Host** am Klavier, liest Noten, Pedale und Velocity und treibt damit:

- **WS2812B LED-Streifen** als Klaviatur-Licht (Anschlag → Flare, Tonhoehe → Farbe, Pedal → Nachgluehen)
- **MQTT/Home Assistant** Discovery (Sensoren, Schalter, Regler, Buttons — alles automatisch)
- **Spielzeit-Tracking** mit konfigurierbarem Tagesziel, Fortschrittsbalken auf den LEDs, Meilenstein-Sounds
- **Ruf-Melodien**: das Klavier erinnert per MIDI-Out ans Ueben (Zeitfenster + Haeufigkeit per HA-Schieber)
- **Aufnahme/Wiedergabe** eigener Stuecke + eingebaute Melodien (Clair de Lune, Nocturne, Entertainer, ...)
- **OTA-Updates** ueber WLAN

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
| Controller | **YD-ESP32-S3** (DevKitC-1-Klon mit CH343 UART, USB-OTG-Loetjumper) |
| LEDs | **WS2812B** (oder SK6812 RGBW), 60-81 LEDs |
| Netzteil | **5 V / 2 A** (FastLED begrenzt auf 1600 mA per Software) |
| USB-Kabel | **USB-C-auf-B-Kabel** direkt vom ESP zum "To Host"-Port am Klavier |

### Pinbelegung (YD-ESP32-S3)
| Anschluss | GPIO / Port |
|---|---|
| LED-Daten (WS2812B) | GPIO 21 |
| USB zum Klavier | **Native USB-C-Buchse** (D-=GPIO19, D+=GPIO20) |
| Flashen / Serial Monitor | **UART-USB-C-Buchse** (CH343, COM-Port am PC) |

### USB-OTG-Loetjumper (zwingend erforderlich!)

Die native USB-C-Buchse des ESP32-S3 ist im Auslieferungszustand eine **Device-Buchse** und liefert
**kein VBUS**. Ohne VBUS erkennt das Klavier keinen USB-Host — es passiert einfach nichts.

Auf der **Rueckseite** des YD-ESP32-S3 befindet sich ein kleiner Loetjumper mit der Beschriftung
**"USB-OTG"**. Dieser muss mit einem Tropfen Loetzinn **gebrueckt** werden. Er verbindet die 5V-Leitung
(vom UART-Port bzw. Netzteil) mit dem VBUS-Pin der nativen USB-Buchse.

> **Achtung:** Den daneben liegenden Jumper **"IN-OUT" NICHT** anruehren — der ist fuer die
> Stromversorgungsrichtung und kann das Board beschaedigen wenn falsch gesetzt.

Nach dem Loeten das **USB-C-auf-B-Kabel** direkt in die native USB-C-Buchse stecken und zum
Klavier fuehren. Kein Adapter, keine externe USB-A-Buchse noetig.

## Anleitung

### Voraussetzungen
- [PlatformIO](https://platformio.org/) (CLI oder VS Code Extension)
- ESP32-S3 Board mit USB-OTG
- WLAN-Zugang
- MQTT-Broker (z.B. Mosquitto in Home Assistant)

### 1. Repository klonen
```bash
git clone https://github.com/bernd780/piano_spark.git
cd piano_spark
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
1. **USB-OTG-Loetjumper** auf der Rueckseite des YD-ESP32-S3 zuloeten (siehe oben)
2. **LED-Streifen** an GPIO 21 + GND + 5V vom Netzteil
3. **USB-C-auf-B-Kabel** von der nativen USB-C-Buchse zum "To Host"-Port am Klavier
4. ESP32 per UART-Buchse (Laptop) oder Netzteil mit Strom versorgen

### Troubleshooting
- **Klavier wird nicht erkannt:** USB-OTG-Loetjumper auf der Rueckseite pruefen — ohne ihn liefert die native Buchse kein VBUS und das Klavier meldet sich nicht an.
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
