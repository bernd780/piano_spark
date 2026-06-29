# Build Guide — piano_spark (ESP32-S3 + WS2812B)

*[Deutsche Version unten / German version below](#bauanleitung--piano_spark-esp32-s3--ws2812b)*

## 0. Check your board (critical!)
- Must be an **ESP32-S3** (USB host capable). Classic ESP32 (WROOM-32) = **will not work**.
- Identification: chip/board label says **"ESP32-S3"**, has a native **USB-C** port on the chip.
- Recommended: **YD-ESP32-S3** (DevKitC-1 clone with CH343 UART and USB-OTG solder jumper).

## 1. Parts
- **YD-ESP32-S3** (or compatible ESP32-S3 DevKit)
- **WS2812B 5V LED strip, 60 LED/m** — 3 connections: **+5V, GND, DIN**
- **5V power supply** (size → step 2)
- **USB-C to B cable** (from ESP native port to piano "To Host")
- Recommended: **~330 Ohm resistor** (data line), **1000 uF capacitor** (across +5V/GND near strip)
- Soldering iron (for the OTG jumper)

## 2. Power budget
At 60 LED/m: **LED count = 60 x strip length in meters.**

| Strip | LEDs (NUM_LEDS) | Power supply |
|---|---|---|
| 1.0 m | 60 | 5V / 4-5 A |
| 1.5 m | 90 | 5V / 6 A |
| 2.0 m | 120 | 5V / 8-10 A |

The firmware limits current to **1600 mA** via `FastLED.setMaxPowerInVoltsAndMilliamps()`, so a **5V / 2A** supply is sufficient for typical ambient use with up to ~80 LEDs.

## 3. Wiring

### 3a. USB-OTG solder jumper (mandatory!)
The native USB-C port ships as a **device port** with **no VBUS output**. The piano won't detect the ESP without VBUS.

On the **back side** of the YD-ESP32-S3, find the solder jumper labeled **"USB-OTG"** and **bridge it with a drop of solder**. This routes 5V to the native USB port's VBUS pin.

> **Warning:** Do **NOT** touch the adjacent **"IN-OUT"** jumper — it controls power direction and can damage the board.

After soldering, connect a **USB-C to B cable** directly from the native USB-C port to the piano's "To Host" port.

### 3b. LED strip (WS2812B)
Follow the **arrow** on the strip (data direction). Connect at the **beginning** (arrow pointing away).

| Strip wire | Connect to |
|---|---|
| **+5V** | Power supply **+5V** |
| **GND** | Power supply **GND** **and** ESP32 **GND** |
| **DIN** | Through **~330 Ohm** to **GPIO 21** (LED_PIN) |

**1000 uF capacitor** between +5V and GND near the strip start (absorbs power-on spikes).

### 3c. Common ground (essential!)
**Power supply GND + ESP32 GND + LED strip GND — all must be connected together.**

### Wiring diagram
```
   5V Power Supply ──┬──────────────────────────► +5V  (LED strip)
                     │
   YD-ESP32-S3       │
     Native USB-C ───[USB-C-to-B cable]──────────► Piano "USB To Host"
       (D-=GPIO19, D+=GPIO20, VBUS via OTG jumper)
     GPIO21 ─[330R]──────────────────────────────► DIN  (strip, follow arrow!)
     UART USB-C ◄── laptop (flashing + serial monitor)
     GND ────────────┴───(common ground: PSU, ESP, strip)───► GND
                                   [1000uF between +5V and GND at strip]
```

## 4. Flash firmware
1. Install [PlatformIO](https://platformio.org/) (CLI or VS Code extension).
2. Clone the repo: `git clone https://github.com/bernd780/piano_spark.git`
3. Create `src/secrets.h` with your WiFi and MQTT credentials (see [README](README.md)).
4. Adjust `src/main.cpp` if needed: `NUM_LEDS`, `LED_PIN`, `MAX_BRIGHT`.
5. Connect ESP via the **UART USB-C port** (not the native port) and flash:
   ```bash
   pio run -t upload
   ```
6. After first flash, OTA is available: `pio run -t upload --upload-port kawai-klavier.local`

## 5. First start & test
- Power on → piano on → USB connected → **play**.
- Expected: light reacts (brighter with stronger touch; bass = warm/red, treble = cool/white; pedal = longer glow).
- Home Assistant: device **"Kawai Klavier"** appears automatically via MQTT.

### Troubleshooting
| Symptom | Check |
|---|---|
| No light at all | Common **GND**? **DIN** at correct end (arrow)? `LED_PIN` correct? `MAX_BRIGHT` > 0? |
| Piano not detected | **USB-OTG solder jumper** bridged? USB-C to B cable in the **native** port (not UART)? |
| ESP keeps rebooting | Power supply too weak / brownout → power ESP separately |
| First LED flickers | Add **330 Ohm** resistor, keep data wire **short** |
| MQTT not connecting | Check serial monitor (115200 baud) for WiFi/MQTT status |

## 6. Safety
- Use a proper 5V power supply with adequate wire gauge for +5V/GND.
- Long strips: inject power at **both ends**.

---

# Bauanleitung — piano_spark (ESP32-S3 + WS2812B)

## 0. Board pruefen (entscheidend!)
- Es **muss ein ESP32-S3** sein (USB-Host). Klassischer ESP32 (WROOM-32) = **geht nicht**.
- Erkennung: Chip-/Board-Aufdruck **"ESP32-S3"**, nativer **USB-C** am Chip.
- Empfohlen: **YD-ESP32-S3** (DevKitC-1-Klon mit CH343 UART und USB-OTG-Loetjumper).

## 1. Teile
- **YD-ESP32-S3** (oder kompatibles ESP32-S3 DevKit)
- **WS2812B 5V LED-Streifen, 60 LED/m** — 3 Anschluesse: **+5V, GND, DIN**
- **5V-Netzteil** (Groesse → Schritt 2)
- **USB-C-auf-B-Kabel** (vom nativen ESP-Port zum Klavier "To Host")
- Empfohlen: **~330 Ohm Widerstand** (Datenleitung), **1000 uF Elko** (an +5V/GND nahe Streifen)
- Loetkolben (fuer den OTG-Jumper)

## 2. Strombudget
Bei 60 LED/m gilt: **LED-Zahl = 60 x Streifenlaenge in Metern.**

| Streifen | LEDs (NUM_LEDS) | Netzteil |
|---|---|---|
| 1,0 m | 60 | 5V / 4-5 A |
| 1,5 m | 90 | 5V / 6 A |
| 2,0 m | 120 | 5V / 8-10 A |

Die Firmware begrenzt den Strom auf **1600 mA** per `FastLED.setMaxPowerInVoltsAndMilliamps()`, daher reicht ein **5V / 2A** Netzteil fuer typischen Ambiente-Betrieb mit bis zu ~80 LEDs.

## 3. Verdrahtung

### 3a. USB-OTG-Loetjumper (zwingend erforderlich!)
Die native USB-C-Buchse ist im Auslieferungszustand eine **Device-Buchse** und liefert **kein VBUS**. Ohne VBUS erkennt das Klavier keinen USB-Host.

Auf der **Rueckseite** des YD-ESP32-S3 den Loetjumper **"USB-OTG"** finden und mit einem Tropfen Loetzinn **gebruecken**. Damit werden die 5V auf den VBUS-Pin der nativen USB-Buchse gelegt.

> **Achtung:** Den daneben liegenden Jumper **"IN-OUT" NICHT** anruehren — der ist fuer die Stromversorgungsrichtung und kann das Board beschaedigen.

Nach dem Loeten ein **USB-C-auf-B-Kabel** direkt von der nativen USB-C-Buchse zum "To Host"-Port am Klavier stecken.

### 3b. LED-Streifen (WS2812B)
Achte auf den **Pfeil** auf dem Streifen (Datenrichtung). Am **Anfang** (Pfeil zeigt weg) anschliessen.

| Streifen | Verbinden mit |
|---|---|
| **+5V** | Netzteil **+5V** |
| **GND** | Netzteil **GND** **und** ESP32 **GND** |
| **DIN** | Ueber **~330 Ohm** an **GPIO 21** (LED_PIN) |

**1000 uF Elko** zwischen +5V und GND nahe am Streifenanfang (Einschaltspitzen abfangen).

### 3c. Gemeinsame Masse (wichtig!)
**Netzteil-GND + ESP32-GND + Streifen-GND — alle miteinander verbinden.**

### Verdrahtungsplan
```
   5V-Netzteil ──────┬──────────────────────────► +5V  (LED-Streifen)
                     │
   YD-ESP32-S3       │
     Native USB-C ───[USB-C-auf-B-Kabel]────────► Klavier "USB To Host"
       (D-=GPIO19, D+=GPIO20, VBUS ueber OTG-Jumper)
     GPIO21 ─[330R]──────────────────────────────► DIN  (Streifen, Pfeilrichtung!)
     UART USB-C ◄── Laptop (Flashen + Serial Monitor)
     GND ────────────┴───(gemeinsame Masse: NT, ESP, Streifen)───► GND
                                   [1000uF zwischen +5V und GND am Streifen]
```

## 4. Firmware flashen
1. [PlatformIO](https://platformio.org/) installieren (CLI oder VS Code Extension).
2. Repo klonen: `git clone https://github.com/bernd780/piano_spark.git`
3. `src/secrets.h` mit WLAN- und MQTT-Zugangsdaten anlegen (siehe [README](README.md)).
4. In `src/main.cpp` bei Bedarf anpassen: `NUM_LEDS`, `LED_PIN`, `MAX_BRIGHT`.
5. ESP ueber den **UART-USB-C-Port** (nicht den nativen Port) anschliessen und flashen:
   ```bash
   pio run -t upload
   ```
6. Nach dem ersten Flash ist OTA verfuegbar: `pio run -t upload --upload-port kawai-klavier.local`

## 5. Erststart & Test
- Netzteil an → Klavier an → USB verbunden → **spielen**.
- Erwartung: Licht reagiert (heller bei kraeftigem Anschlag; Bass warm/rot, Hoehen kuehl/weiss; Pedal = laengeres Nachgluehen).
- Home Assistant: Device **"Kawai Klavier"** erscheint automatisch per MQTT.

### Troubleshooting
| Symptom | Pruefen |
|---|---|
| Gar kein Licht | Gemeinsame **GND**? **DIN** am richtigen Ende (Pfeil)? `LED_PIN` korrekt? `MAX_BRIGHT` > 0? |
| Klavier wird nicht erkannt | **USB-OTG-Loetjumper** gebrueckt? USB-C-auf-B-Kabel im **nativen** Port (nicht UART)? |
| ESP startet staendig neu | Netzteil zu schwach / Brownout → ESP separat versorgen |
| Erste LED flackert | **330 Ohm** Widerstand rein, Datenleitung **kurz** halten |
| MQTT verbindet nicht | Serial Monitor (115200 Baud) auf WLAN-/MQTT-Status pruefen |

## 6. Sicherheit
- Ordentliches 5V-Netzteil, ausreichender Kabelquerschnitt fuer +5V/GND.
- Lange Streifen: Strom an **beiden Enden** einspeisen.
