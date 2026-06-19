# Bauanleitung — Klavier‑Ambientelicht (ESP32‑S3 + BTF WS2812B 60 LED/m)

## 0. Board prüfen (entscheidend!)
- Es **muss ein ESP32‑S3** sein (USB‑Host). Klassischer ESP32 (WROOM‑32) = **geht nicht**.
- Erkennung: Chip-/Board‑Aufdruck **„ESP32‑S3"**, nativer **USB‑C** am Chip.
- Hast du das DevKit‑Board: prima, wir verdrahten den USB‑Host selbst (Variante B).

## 1. Teile
- **ESP32‑S3** (du)
- **BTF WS2812B 5 V, 60 LED/m, schwarze PCB** (du) — 3 Anschlüsse: **+5 V, GND, DIN**
- **5‑V‑Netzteil** (Größe → Schritt 2)
- **USB‑A‑Buchse** (Einbau/Breakout) + vorhandenes **USB‑A→B‑Kabel** zum Klavier
- Empfohlen: **~330 Ω** (in die Datenleitung), **1000 µF** Elko (an +5 V/GND nahe Streifen), optional **Level‑Shifter 74AHCT125** (3,3 V→5 V Daten)
- Dupont‑/Litzen, Lötkolben

## 2. Strombudget → Netzteil
Bei 60 LED/m gilt: **LED‑Zahl N = 60 × Streifenlänge in Metern.**
- Theoretisches Maximum (alles weiß, voll): **N × 60 mA**.
- Für **Ambiente** leuchten selten alle voll → real viel weniger; trotzdem Netzteil großzügig.

| Streifen | LEDs (NUM_LEDS) | Netzteil‑Empfehlung |
|---|---|---|
| 1,0 m | 60 | 5 V / 4–5 A |
| 1,5 m | 90 | 5 V / 6 A |
| 2,0 m | 120 | 5 V / 8–10 A |
| 3,0 m | 180 | 5 V / 10–15 A (beidseitig einspeisen) |

`MAX_BRIGHT` im Sketch begrenzt die Spitzenlast zusätzlich.

## 3. Verdrahtung

### 3a. USB zum Klavier (USB‑Host, selbst verdrahtet)
Nativer USB des S3: **GPIO19 = D−**, **GPIO20 = D+**.

| USB‑A‑Buchse | verbinden mit |
|---|---|
| **VBUS (+5 V)** | **+5 V vom Netzteil** ← wichtig, der S3 liefert kein VBUS! |
| **D−** | GPIO19 |
| **D+** | GPIO20 |
| **GND** | gemeinsame Masse |

Dann **USB‑A→B‑Kabel** von der Buchse zum Klavier‑Port **„USB To Host"**.
Den ESP **programmierst/versorgst** du über den **zweiten** USB‑Port des DevKits (UART/„COM"‑Port).

### 3b. LED‑Streifen (BTF WS2812B)
Achte auf den **Pfeil** auf dem Streifen = Datenrichtung. Am **Anfang** (Pfeil zeigt weg) anschließen.

| Streifen | verbinden mit |
|---|---|
| **+5 V** | Netzteil **+5 V** |
| **GND** | Netzteil **GND** **und** ESP32 **GND** |
| **DIN** | über **~330 Ω** an **LED_PIN** (Default **GPIO21**) |

**1000 µF Elko** zwischen +5 V und GND nahe am Streifenanfang (Einschaltspitzen abfangen).
Optional **Level‑Shifter** in die Datenleitung (bei kurzer Strecke oft auch ohne ok).

### 3c. Das Wichtigste: gemeinsame Masse
**Netzteil‑GND + ESP32‑GND + Streifen‑GND + USB‑A‑GND ALLE miteinander verbinden.**

### Blockbild
```
   5V-Netzteil ──┬──────────────────────────► +5V  (LED-Streifen)
                 ├── VBUS ─► USB-A-Buchse ─[A-B-Kabel]─► Klavier "USB To Host"
                 │
   ESP32-S3      │
     GPIO20 (D+) ────────► USB-A  D+
     GPIO19 (D-) ────────► USB-A  D-
     GPIO21 ─[330Ω]──────────────────────────► DIN  (Streifen, Pfeilrichtung!)
     5V-In ◄──────────────┘
     GND ─────────────────┴───(gemeinsame Masse: NT, ESP, Streifen, USB-A)───► GND
                                   [1000µF zwischen +5V und GND am Streifen]
```

## 4. Firmware flashen
1. **Arduino IDE** → Boardpaket **„esp32 by Espressif" ≥ 3.0.0**.
2. Board: **ESP32S3 Dev Module**. **Tools → USB Mode → „USB Host"**. *(wichtig!)*
3. Libraries (Library Manager): **ESP32_Host_MIDI** (sauloverissimo), **FastLED**.
4. In `led_piano.ino` oben anpassen:
   - `NUM_LEDS` = deine LED‑Zahl (60 × Meter)
   - `LED_PIN` = 21 (oder dein freier Pin)
   - **`BOARD_ESP32S3_USB_OTG 0`** (weil selbst verdrahtet; `1` nur beim ESP32‑S3‑USB‑OTG‑Board)
   - `MAX_BRIGHT` nach Geschmack/Netzteil
5. Hochladen über den **UART/Programmier‑Port**.

## 5. Erststart & Test
- Netzteil an → Klavier an → USB verbunden → **spielen**.
- Erwartung: Licht reagiert (heller bei kräftigem Anschlag; Bass warm, Höhen kühl; Pedal = Nachglühen).

### Troubleshooting
| Symptom | Prüfen |
|---|---|
| Gar kein Licht | gemeinsame **GND**? **DIN** am richtigen Ende (Pfeil)? `LED_PIN` korrekt? `MAX_BRIGHT` > 0? |
| Klavier wird nicht erkannt | **VBUS 5 V** an der USB‑A‑Buchse? **USB Mode = USB Host** gesetzt? Wirklich **S3**? |
| ESP startet ständig neu | Netzteil zu schwach / ESP separat versorgen (Brownout) |
| Erste LED flackert/falsch | **330 Ω** rein, Datenleitung **kurz**, ggf. **Level‑Shifter** |

## 6. Sicherheit
- Ordentliches 5‑V‑Netzteil, ausreichender Kabelquerschnitt für +5 V/GND.
- Lange Streifen **an beiden Enden** einspeisen.
