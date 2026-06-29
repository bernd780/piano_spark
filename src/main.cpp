/*
 * Kawai CA63 -> ESP32-S3 USB-Host -> Home Assistant (WLAN/MQTT) + Klaviatur-Licht (FastLED).
 * EIN Board, EIN MIDI-Stream: speist HA-Entitaeten UND treibt die LEDs positionsbasiert.
 *
 * LED-Effekt: jede Taste mappt nach Tonhoehe auf eine LED-Position (A0->Anfang, C8->Ende).
 *   Anschlag -> die LED dort wird heller (nach Velocity), klingt dann ab (Pedal = laenger).
 *   Farbe nach Tonhoehe: tief = rot, hoch = weiss (Saettigung sinkt mit der Hoehe).
 *
 * Board: YD-ESP32-S3 (USB-OTG-Jumper gebrueckt). Klavier an der NATIVEN USB-Buchse.
 * Libs:  ESP32_Host_MIDI, PubSubClient, ArduinoJson v7, FastLED.
 */
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <ESP32_Host_MIDI.h>
#include <USBConnection.h>
#include "secrets.h"          // WIFI_SSID/PASS, MQTT_HOST/PORT/USER/PASS
#include "midi_evt.h"         // struct MidiEvt (gemeinsam)
#include "clair_de_lune.h"    // CLAIR[]    - Debussy, Clair de Lune
#include "nocturne.h"         // NOCTURNE[] - Chopin, Nocturne Op.9/2
#include "comptine.h"         // COMPTINE[] - Yann Tiersen, Comptine (Amelie)
#include "wgwh.h"             // WGWH[]       - "Wo gehen wir hin..." (Akkord-Arrangement, Maus)
#include "entertainer.h"      // ENTERTAINER[] - Scott Joplin
#include "moonlight.h"        // MOONLIGHT[]   - Beethoven, Mondscheinsonate 1. Satz

// ===================== Konfig =====================
#define DEVICE_ID    "kawai_klavier"
#define DEVICE_NAME  "Kawai Klavier"

#define LED_PIN      21
#define NUM_LEDS     81
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
#define MAX_BRIGHT   210        // Obergrenze; das Strom-Limit unten deckelt zusaetzlich dynamisch
#define LED_REVERSED 0          // 1 setzen, falls Bass/Hoehe spiegelverkehrt am Streifen liegen
#define AMBIENT_MAX  34         // Helligkeit des bunten Hintergrund-"Klimbim" (dunkel halten)
#define IDLE_TIMEOUT_MS 20000   // nach so langer Stille -> Knight-Rider-Lauflicht
// ==================================================

// ----- Live-regelbare LED-Parameter (per HA-Schieber, in NVS gespeichert) -----
#include <Preferences.h>
Preferences prefs;
uint8_t cfgMaxBright = MAX_BRIGHT;   // 0..255  maximale Helligkeit
uint8_t cfgAmbient   = AMBIENT_MAX;  // 0..120  Klimbim-Intensitaet
uint8_t cfgScanner   = 120;          // 0..255  Lauflicht-Intensitaet (0 = aus)
uint8_t cfgMode      = 0;             // Lichtmodus: 0=Bunt (Standard), 1=Classic (gold), 2=Party (Regenbogen)
uint16_t cfgGoalMin  = 30;            // Tages-Spielziel in Minuten (per HA-Schieber)
uint8_t  cfgCallFrom = 14;           // Aufforderungs-Zeitfenster: ab Stunde (0-23)
uint8_t  cfgCallTo   = 20;           // Aufforderungs-Zeitfenster: bis Stunde (0-23)
uint8_t  cfgStandbyMin = 15;         // Minuten Lauflicht bevor Standby (1-60)
uint8_t  cfgCallFreq   = 3;          // Aufforderungs-Haeufigkeit: 1=selten(8-12min) 5=oft(1-2min)
uint8_t  cfgSparks     = 0;          // Funken-Intensitaet 0=aus, 255=voll
uint8_t  cfgCallSound  = 3;          // Audio-Erinnerung: 0=aus, 1=selten(~10min), 5=oft(~2min)
uint8_t  callSoundIdx  = 0;          // rotiert durch die Ruf-Melodien

// ----- Spielzeit-Tracking -----
uint32_t practiceMs      = 0;         // heutige Spielzeit in ms
uint32_t lastPlayingMs   = 0;         // letzter Zeitpunkt mit aktivem Tastenanschlag
bool     goalReached     = false;     // Tagesziel heute schon erreicht?
bool     goalCelebration = false;     // Feier-Animation laeuft?
uint32_t celebrationStart= 0;

// ----- Standby-Logik -----
#define WAKEUP_SHOW_MS      8000                  // 8s Lauflicht bei Aufwachen

// NVS-Persistierung der Spielzeit
uint32_t lastPracticeSaveMs = 0;
#define  PRACTICE_SAVE_INTERVAL 60000             // alle 60s in NVS sichern
// ----- Fortschrittsbalken-Overlay -----
bool     cfgProgressBar    = true;     // dauerhaft sichtbar (HA-Schalter)
uint8_t  lastMilestonePct  = 0;       // letzter erreichter 10%-Meilenstein
uint32_t milestoneFlashUntil = 0;     // Meilenstein-Aufleuchten bis (millis)
uint32_t milestoneFlashStart = 0;
int      prevFillEnd       = 0;       // letzte gefuellte LED-Position (fuer Wachstums-Erkennung)
#define  PROGRESS_BRIGHT      12      // Grund-Helligkeit (dauerhaft, dezent)
#define  PROGRESS_GROW_BRIGHT 22      // pulsierende neue LEDs
#define  MILESTONE_BRIGHT     60      // Helligkeit bei Meilenstein-Flash
#define  MILESTONE_FLASH_MS 4000      // Dauer des Aufblitzens

// ----- Farbtest-Modus -----
uint32_t colorTestUntil = 0;          // LEDs eingefroren bis (millis), 0=inaktiv
CRGB     colorTestVal   = CRGB::Black;

bool     standby         = false;
uint32_t standbyEnteredMs= 0;
uint32_t nextWakeupMs    = 0;
uint32_t wakeupShowUntil = 0;                     // kurzes Aufwachen laeuft bis hier

// ----- Uhrzeit (NTP) -----
#include <time.h>
int currentHour() {
  struct tm ti;
  if (!getLocalTime(&ti, 0)) return -1;
  return ti.tm_hour;
}

String T_STATE = String(DEVICE_ID) + "/state";
String T_AVAIL = String(DEVICE_ID) + "/availability";
String T_EVENT = String(DEVICE_ID) + "/event";

WiFiClient   net;
PubSubClient mqtt(net);
USBConnection usbHost;          // 'midiHandler' ist global in der Lib

CRGB  leds[NUM_LEDS];
float ledEnergy[NUM_LEDS];      // pro LED: 0..1 Helligkeits-Energie (zero-init)

// ----- Zustand: Home Assistant -----
struct Voice { bool down; uint8_t vel; };
Voice    notes[128];
int      activeCount   = 0;
String   lastNote      = "-";
uint8_t  lastNoteNum   = 0, lastVelocity = 0;
bool     sustain       = false;  uint8_t sustainPos = 0;
uint8_t  sostenutoPos  = 0;
bool     sostenuto     = false,  soft = false;
uint8_t  softPos       = 0;
int      lastProgram   = -1;
uint32_t notesToday    = 0;
uint32_t notesPlayed   = 0;          // nur selbst gespielte Toene (ohne Wiedergabe)
uint32_t lastNoteMs    = 0;
bool     playing       = false,  connectedPiano = false;
uint32_t lastPublishMs = 0;      bool dirty = true;
uint32_t lastFrame     = 0;

// ----- Langdruck-Erkennung fuer Aufnahme -----
#define LONGPRESS_MS 2000
uint32_t lowKeyDownMs  = 0;          // wann A0 (21) gedrueckt, 0=nicht gedrueckt
uint32_t highKeyDownMs = 0;          // wann C8 (108) gedrueckt
bool     lowKeyFired   = false;      // Langdruck schon ausgeloest?
bool     highKeyFired  = false;

// ----- Zustand: Aufnahme + Vorspiel (Puffer + Funktionen weiter unten) -----
bool     melPlaying = false;
bool     recording = false, recPlaying = false;
uint32_t recStartMs = 0, recPlayStartMs = 0;
int      recCount = 0, recPlayIdx = 0;

const char* NOTE_NAMES[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","H"};
String noteName(uint8_t n){ return String(NOTE_NAMES[n % 12]) + String((int)(n / 12) - 1); }

// ----- Tonhoehe <-> LED-Position -----
// Klavierumfang A0(21)..C8(108). t=0 Bass, t=1 Hoehe.
int noteToLed(uint8_t note) {
  float t = (note - 21) / 87.0f; if (t < 0) t = 0; if (t > 1) t = 1;
#if LED_REVERSED
  t = 1.0f - t;
#endif
  int i = (int)(t * (NUM_LEDS - 1) + 0.5f);
  if (i < 0) i = 0; if (i >= NUM_LEDS) i = NUM_LEDS - 1;
  return i;
}
float ledPitchFrac(int i) {           // welche Tonhoehe steht an dieser LED (0=Bass,1=Hoehe)
  float t = (float)i / (NUM_LEDS - 1);
#if LED_REVERSED
  t = 1.0f - t;
#endif
  return t;
}
void addEnergy(int i, float amt) {
  if (i < 0 || i >= NUM_LEDS) return;
  ledEnergy[i] += amt; if (ledEnergy[i] > 1.0f) ledEnergy[i] = 1.0f;
}

// ----- Home Assistant MQTT Discovery -----
void publishDiscovery() {
  auto deviceBlock = [](JsonObject dev){
    JsonArray ids = dev["ids"].to<JsonArray>(); ids.add(DEVICE_ID);
    dev["name"] = DEVICE_NAME; dev["mf"] = "Kawai"; dev["mdl"] = "Digitalpiano (USB-MIDI)";
  };
  struct Ent { const char* comp; const char* key; const char* name; const char* tmpl;
               const char* dclass; const char* icon; const char* unit; };
  Ent ents[] = {
    {"binary_sensor","connected","Verbunden",     "{{ value_json.connected | lower }}",   "connectivity", nullptr,              nullptr},
    {"binary_sensor","playing",  "Spielt",         "{{ value_json.playing | lower }}",     "running",      "mdi:piano",          nullptr},
    {"binary_sensor","sustain",  "Sustain-Pedal",  "{{ value_json.sustain | lower }}",     nullptr,        "mdi:foot-print",     nullptr},
    {"binary_sensor","sostenuto","Sostenuto-Pedal","{{ value_json.sostenuto | lower }}",  nullptr,        "mdi:foot-print",     nullptr},
    {"sensor",       "sostenuto_pos","Sostenuto-Stellung","{{ value_json.sostenuto_pos }}",nullptr,       "mdi:foot-print",     nullptr},
    {"binary_sensor","soft",     "Soft-Pedal",     "{{ value_json.soft | lower }}",       nullptr,        "mdi:foot-print",     nullptr},
    {"sensor",       "soft_pos", "Soft-Stellung",  "{{ value_json.soft_pos }}",           nullptr,        "mdi:foot-print",     nullptr},
    {"sensor",       "sustain_pos","Pedalstellung","{{ value_json.sustain_pos }}", nullptr,        "mdi:foot-print",     nullptr},
    {"sensor",       "last_note", "Letzte Note",   "{{ value_json.last_note }}",   nullptr,        "mdi:music-note",     nullptr},
    {"sensor",       "velocity",  "Anschlagstaerke","{{ value_json.velocity }}",   nullptr,        "mdi:gauge",          nullptr},
    {"sensor",       "active",    "Aktive Tasten", "{{ value_json.active }}",      nullptr,        "mdi:keyboard",       nullptr},
    {"sensor",       "program",   "Klang (Program)","{{ value_json.program }}",    nullptr,        "mdi:tune",           nullptr},
    {"sensor",       "notes_today","Toene heute",  "{{ value_json.notes_today }}", nullptr,        "mdi:counter",        nullptr},
    {"sensor",       "notes_played","Toene selbst gespielt","{{ value_json.notes_played }}",nullptr,"mdi:account-music",  nullptr},
    {"binary_sensor","recording", "Aufnahme laeuft","{{ value_json.recording | lower }}",  "running",      "mdi:record-circle",  nullptr},
    {"sensor",       "practice",  "Spielzeit heute","{{ value_json.practice }}",   nullptr,        "mdi:timer-music",    "min"},
    {"sensor",       "goal_pct",  "Ziel-Fortschritt","{{ value_json.goal_pct }}",  nullptr,        "mdi:percent-circle", "%"},
    {"binary_sensor","goal_ok",   "Tagesziel erreicht","{{ value_json.goal_ok | lower }}",  nullptr,        "mdi:check-circle",  nullptr},
  };
  for (auto& e : ents) {
    JsonDocument d;
    d["name"]=e.name; d["uniq_id"]=String(DEVICE_ID)+"_"+e.key;
    d["stat_t"]=T_STATE; d["val_tpl"]=e.tmpl;
    d["avty_t"]=T_AVAIL; d["pl_avail"]="online"; d["pl_not_avail"]="offline";
    if (e.dclass) d["dev_cla"]=e.dclass;
    if (e.icon)   d["icon"]=e.icon;
    if (e.unit)   d["unit_of_meas"]=e.unit;
    if (String(e.comp)=="binary_sensor"){ d["pl_on"]="true"; d["pl_off"]="false"; }
    deviceBlock(d["dev"].to<JsonObject>());
    String topic = String("homeassistant/")+e.comp+"/"+DEVICE_ID+"/"+e.key+"/config";
    String payload; serializeJson(d, payload);
    mqtt.publish(topic.c_str(), payload.c_str(), true);   // retained
  }

  // Buttons zum Vorspielen: Druck -> payload_press an kawai_klavier/play
  struct Btn { const char* key; const char* name; const char* payload; const char* icon; };
  Btn btns[] = {
    {"play_haenschen", "Haenschen klein spielen",    "haenschen", "mdi:music-note"},
    {"play_entchen",   "Alle meine Entchen spielen", "entchen",   "mdi:music-note"},
    {"play_clair",     "Clair de Lune (Debussy)",    "clair",     "mdi:weather-night"},
    {"play_nocturne",  "Chopin Nocturne Op.9/2",     "nocturne",  "mdi:music-clef-treble"},
    {"play_comptine",  "Comptine (Amelie)",          "comptine",  "mdi:movie-open"},
    {"play_wgwh",      "Schlaflied (Maus)",          "wgwh",        "mdi:sleep"},
    {"play_entertainer","The Entertainer (Joplin)",  "entertainer", "mdi:piano"},
    {"play_moonlight", "Mondscheinsonate",           "moonlight",   "mdi:weather-night"},
    {"rec_start",      "Aufnahme starten",           "rec",       "mdi:record-circle"},
    {"rec_stop",       "Aufnahme stoppen",           "recstop",   "mdi:stop-circle"},
    {"rec_play",       "Aufnahme abspielen",         "recplay",   "mdi:play-circle"},
    {"play_stop",      "Alles stoppen",              "stop",      "mdi:stop"},
    {"test_add10",     "Test: +10 Min Spielzeit",    "testadd10", "mdi:timer-plus"},
    {"test_reset",     "Test: Spielzeit Reset",      "testreset", "mdi:timer-off"},
    {"test_celebrate", "Test: Feier-Animation",      "testcelebrate","mdi:party-popper"},
    {"test_green",     "Test: Gruen",                "testgreen",    "mdi:palette"},
    {"test_red",       "Test: Rot",                  "testred",      "mdi:palette"},
    {"test_blue",      "Test: Blau",                 "testblue",     "mdi:palette"},
    {"test_white",     "Test: Weiss",                "testwhite",    "mdi:palette"},
    {"test_goalgreen", "Test: Ziel=erreicht",        "testgoalgreen","mdi:check"},
    {"test_coin",      "Test: Coin-Sound",           "testcoin",     "mdi:music-note"},
    {"test_fanfare",   "Test: Fanfare",              "testfanfare",  "mdi:trumpet"},
    {"test_call",      "Test: Naechster Ruf",       "testcall",     "mdi:bell-ring"},
    {"test_call_prev", "Test: Vorheriger Ruf",      "testcallprev", "mdi:skip-previous"},
  };
  for (auto& b : btns) {
    JsonDocument d;
    d["name"]    = b.name;
    d["uniq_id"] = String(DEVICE_ID) + "_" + b.key;
    d["cmd_t"]   = String(DEVICE_ID) + "/play";
    d["payload_press"] = b.payload;
    d["avty_t"]  = T_AVAIL; d["pl_avail"]="online"; d["pl_not_avail"]="offline";
    d["icon"]    = b.icon;
    deviceBlock(d["dev"].to<JsonObject>());
    String topic = String("homeassistant/button/") + DEVICE_ID + "/" + b.key + "/config";
    String payload; serializeJson(d, payload);
    mqtt.publish(topic.c_str(), payload.c_str(), true);   // retained
  }

  // Regler (number / Schieber) fuer die LED-Intensitaeten
  struct Num { const char* key; const char* name; int mx; const char* icon; };
  Num nums[] = {
    {"maxbright", "Max. Helligkeit",       255, "mdi:brightness-6"},
    {"ambient",   "Klimbim-Intensitaet",   120, "mdi:shimmer"},
    {"scanner",   "Lauflicht-Intensitaet", 255, "mdi:run-fast"},
    {"goalmin",   "Tages-Spielziel (Min)",120, "mdi:target"},
    {"callfrom",  "Aufforderung ab (Std)", 23, "mdi:clock-start"},
    {"callto",    "Aufforderung bis (Std)",23, "mdi:clock-end"},
    {"standbymin","Lauflicht-Dauer (Min)", 60, "mdi:timer-sand"},
    {"callfreq",  "Aufforderungs-Haeufigkeit",5,"mdi:bell-ring"},
    {"sparks",    "Funken-Intensitaet",    255,"mdi:shimmer"},
    {"callsound", "Audio-Erinnerung",       5,"mdi:bell-ring-outline"},
  };
  for (auto& n : nums) {
    JsonDocument d;
    d["name"]    = n.name;
    d["uniq_id"] = String(DEVICE_ID) + "_" + n.key;
    d["cmd_t"]   = String(DEVICE_ID) + "/set/" + n.key;
    d["stat_t"]  = String(DEVICE_ID) + "/num/" + n.key;
    d["min"] = 0; d["max"] = n.mx; d["step"] = 1; d["mode"] = "slider";
    d["avty_t"]  = T_AVAIL; d["pl_avail"]="online"; d["pl_not_avail"]="offline";
    d["icon"]    = n.icon;
    deviceBlock(d["dev"].to<JsonObject>());
    String topic = String("homeassistant/number/") + DEVICE_ID + "/" + n.key + "/config";
    String payload; serializeJson(d, payload);
    mqtt.publish(topic.c_str(), payload.c_str(), true);
  }

  // Switch: Fortschrittsbalken
  {
    JsonDocument d;
    d["name"]    = "Fortschrittsbalken";
    d["uniq_id"] = String(DEVICE_ID) + "_progressbar";
    d["cmd_t"]   = String(DEVICE_ID) + "/set/progressbar";
    d["stat_t"]  = String(DEVICE_ID) + "/num/progressbar";
    d["pl_on"]   = "ON"; d["pl_off"] = "OFF";
    d["avty_t"]  = T_AVAIL; d["pl_avail"]="online"; d["pl_not_avail"]="offline";
    d["icon"]    = "mdi:progress-check";
    deviceBlock(d["dev"].to<JsonObject>());
    String topic = String("homeassistant/switch/") + DEVICE_ID + "/progressbar/config";
    String payload; serializeJson(d, payload);
    mqtt.publish(topic.c_str(), payload.c_str(), true);
  }

  // alten Classic-Schalter entfernen (durch Lichtmodus-Dropdown ersetzt)
  mqtt.publish((String("homeassistant/switch/") + DEVICE_ID + "/classic/config").c_str(), "", true);

  // Lichtmodus: Bunt / Classic / Party (HA-Select / Dropdown)
  {
    JsonDocument d;
    d["name"]    = "Lichtmodus";
    d["uniq_id"] = String(DEVICE_ID) + "_mode";
    d["cmd_t"]   = String(DEVICE_ID) + "/set/mode";
    d["stat_t"]  = String(DEVICE_ID) + "/num/mode";
    JsonArray opts = d["options"].to<JsonArray>();
    opts.add("Bunt"); opts.add("Classic"); opts.add("Party");
    d["avty_t"]  = T_AVAIL; d["pl_avail"]="online"; d["pl_not_avail"]="offline";
    d["icon"]    = "mdi:palette";
    deviceBlock(d["dev"].to<JsonObject>());
    String topic = String("homeassistant/select/") + DEVICE_ID + "/mode/config";
    String payload; serializeJson(d, payload);
    mqtt.publish(topic.c_str(), payload.c_str(), true);
  }
}

void publishState(bool force=false) {
  uint32_t now = millis();
  if (!force) {
    uint32_t minInterval = (recPlaying || melPlaying) ? 500 : 75;
    if (now - lastPublishMs < minInterval) return;
    if (!dirty && (now - lastPublishMs) < 1000) return;
  }
  lastPublishMs = now; dirty = false;
  JsonDocument d;
  d["connected"]=connectedPiano; d["playing"]=playing;
  d["last_note"]=lastNote; d["last_note_num"]=lastNoteNum; d["velocity"]=lastVelocity;
  d["active"]=activeCount; d["sustain"]=sustain; d["sustain_pos"]=sustainPos;
  d["sostenuto"]=sostenuto; d["sostenuto_pos"]=sostenutoPos; d["soft"]=soft; d["soft_pos"]=softPos; d["program"]=lastProgram; d["notes_today"]=notesToday; d["notes_played"]=notesPlayed; d["recording"]=recording;
  d["practice"]=(uint32_t)(practiceMs / 60000);
  d["goal_pct"]= cfgGoalMin > 0 ? min((int)(practiceMs * 100 / ((uint32_t)cfgGoalMin * 60000UL)), 100) : 0;
  d["goal_ok"]=goalReached;
  String payload; serializeJson(d, payload);
  mqtt.publish(T_STATE.c_str(), payload.c_str(), true);
}

// ----- LED: Anschlag setzt Energie an der Notenposition (+ weiche Nachbarn) -----
void ledOnNote(uint8_t note, uint8_t vel) {
  int pos = noteToLed(note);
  float b = vel / 127.0f;                 // Anschlagstaerke 0..1
  addEnergy(pos,     b);
  addEnergy(pos - 1, b * 0.5f);
  addEnergy(pos + 1, b * 0.5f);
  addEnergy(pos - 2, b * 0.22f);
  addEnergy(pos + 2, b * 0.22f);
}

// ----- LED-Render: Klaviatur-Licht (buntes Klimbim + Noten-Flares rot->weiss) -----
void renderKeyboard(uint32_t now, float dt) {
  float decay = sustain ? 0.5f : 2.2f;
  for (int i = 0; i < NUM_LEDS; i++) {
    ledEnergy[i] -= ledEnergy[i] * decay * dt;
    if (ledEnergy[i] < 0) ledEnergy[i] = 0;
    // Klimbim-Hintergrund (cfgAmbient==0 -> ganz aus; classic -> dezentes Gold statt Regenbogen)
    uint8_t shim = sin8((uint8_t)(i * 12) + (uint8_t)(now / 7));
    uint8_t aval = cfgAmbient ? (uint8_t)(4 + scale8(shim, cfgAmbient)) : 0;
    uint8_t ahue, asat;
    if      (cfgMode == 1) { ahue = 38; asat = 120; }                          // Classic: gold
    else if (cfgMode == 2) { ahue = (uint8_t)(i * 4 + now / 15); asat = 255; }  // Party: schnell + satt
    else                   { ahue = (uint8_t)(i * 3 + now / 40); asat = 220; }  // Bunt
    CRGB ambient = CHSV(ahue, asat, aval);
    uint8_t fval = (uint8_t)(ledEnergy[i] * 255.0f);
    CRGB flare;
    if      (cfgMode == 1) flare = CHSV(36, 110, fval);                                           // Classic: gold
    else if (cfgMode == 2) flare = CHSV((uint8_t)(ledPitchFrac(i) * 200 + now / 20), 255, fval);  // Party: voller Regenbogen
    else                   flare = CHSV(0, (uint8_t)(255.0f * (1.0f - ledPitchFrac(i))), fval);   // Standard: rot->weiss
    leds[i] = ambient + flare;                        // additiv: Flare ueber Klimbim
  }
  if (cfgSparks && cfgMode == 2 && cfgAmbient && random8() < scale8(14, cfgSparks)) {
    leds[random16(NUM_LEDS)] += CHSV(random8(), 255, scale8(100, cfgSparks));
  }
  // Fortschrittsbalken-Overlay: dauerhaft sichtbar, dezent
  if (cfgGoalMin > 0 && !goalReached && cfgProgressBar) {
    float pct = (float)practiceMs / ((uint32_t)cfgGoalMin * 60000UL);
    if (pct > 1.0f) pct = 1.0f;
    int fillEnd = (int)(pct * (NUM_LEDS - 1));
    bool flashing = (milestoneFlashUntil > 0 && now < milestoneFlashUntil);
    float flashFade = 1.0f;
    if (flashing) {
      uint32_t elapsed = now - milestoneFlashStart;
      uint32_t remaining = milestoneFlashUntil - now;
      if (elapsed < 300) flashFade = elapsed / 300.0f;
      else if (remaining < 600) flashFade = remaining / 600.0f;
    }
    for (int i = 0; i <= fillEnd; i++) {
      float ledPct = (float)i / max(fillEnd, 1);
      uint8_t hue = (uint8_t)(20 + (76.0f * ledPct));   // orange -> gruen
      uint8_t sat = 255;
      uint8_t v = PROGRESS_BRIGHT;
      // neue LEDs (seit letztem Frame gewachsen) pulsieren sanft
      if (i > prevFillEnd) {
        uint8_t pulse = sin8((uint8_t)(now / 6));
        v = PROGRESS_BRIGHT + scale8(pulse, PROGRESS_GROW_BRIGHT - PROGRESS_BRIGHT);
      }
      // Meilenstein-Flash: heller, gruener
      if (flashing) {
        hue = 96; sat = (uint8_t)(ledPct * 255);
        v = PROGRESS_BRIGHT + (uint8_t)((MILESTONE_BRIGHT - PROGRESS_BRIGHT) * flashFade);
      }
      leds[i] += CHSV(hue, sat, v);
    }
    prevFillEnd = fillEnd;
  }
  // Aufnahme-/Wiedergabe-Indikator
  if (recording) {
    uint8_t pulse = sin8((uint8_t)(now / 3));
    uint8_t v = 40 + scale8(pulse, 120);
    for (int i = 0; i < 5 && i < NUM_LEDS; i++)
      leds[i] = CHSV(0, 255, (uint8_t)(v * (5 - i) / 5));
  }
  if (recPlaying) {
    uint8_t pulse = sin8((uint8_t)(now / 3));
    uint8_t v = 40 + scale8(pulse, 120);
    for (int i = 0; i < 5 && i < NUM_LEDS; i++)
      leds[NUM_LEDS - 1 - i] = CHSV(96, 255, (uint8_t)(v * (5 - i) / 5));
  }
}

// ----- LED-Render: Knight-Rider-Lauflicht -----
// goalDone steuert die Farbe: false = bunt ohne gruen (Aufforderung), true = gruen (geschafft!)
void renderKnightRider(uint32_t now, bool goalDone) {
  if (cfgScanner == 0) { fill_solid(leds, NUM_LEDS, CRGB::Black); return; }
  fadeToBlackBy(leds, NUM_LEDS, 32);
  const float halfMs = goalDone ? 1500.0f : 1200.0f;
  float s = 0.5f + 0.5f * sinf((float)now * PI / halfMs);
  int pos = (int)(s * (NUM_LEDS - 1) + 0.5f);
  uint8_t hue, sat, sprd;
  if (goalDone) {
    hue = 96; sat = 255; sprd = 0;               // ausschliesslich gruen
  } else if (cfgMode == 1) {
    hue = 36; sat = 90; sprd = 0;
  } else if (cfgMode == 2) {
    hue = (uint8_t)(pos*(512/NUM_LEDS) + now/12); sat = 255; sprd = 16;
  } else {
    // bunt, aber gruen (hue 80-150) rausdrehen -> rot/orange/pink/lila
    uint8_t raw = (uint8_t)(pos*(256/NUM_LEDS) + now/30);
    hue = (raw < 80) ? raw : (uint8_t)(raw + 70);  // Luecke um gruen
    sat = 255; sprd = 8;
  }
  uint8_t headV = cfgScanner;
  for (int d = -2; d <= 2; d++) {
    int j = pos + d; if (j < 0 || j >= NUM_LEDS) continue;
    uint8_t v = (d == 0) ? headV : (abs(d) == 1 ? (uint8_t)(headV * 0.55f) : (uint8_t)(headV * 0.25f));
    leds[j] += CHSV((uint8_t)(hue + d * sprd), sat, v);
  }
  // Spielerisch: gelegentliche Funken bei nicht erreichtem Ziel
  if (cfgSparks && !goalDone && random8() < scale8(8, cfgSparks)) {
    uint8_t fhue = random8();
    if (fhue >= 80 && fhue <= 150) fhue += 70;
    leds[random16(NUM_LEDS)] += CHSV(fhue, 255, scale8(90, cfgSparks));
  }
}

// ----- LED-Render: Feier-Animation wenn Tagesziel erreicht (15s) -----
#define CELEBRATION_MS 15000
void renderCelebration(uint32_t now, uint32_t start) {
  uint32_t elapsed = now - start;
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  if (elapsed < 3000) {
    // Phase 1 (0-3s): Regenbogen-Explosion von Mitte nach aussen
    float progress = elapsed / 3000.0f;
    int reach = (int)(progress * (NUM_LEDS / 2));
    int mid = NUM_LEDS / 2;
    for (int d = -reach; d <= reach; d++) {
      int j = mid + d; if (j < 0 || j >= NUM_LEDS) continue;
      float dist = (float)abs(d) / (NUM_LEDS / 2);
      uint8_t hue = (uint8_t)(j * 5 + now / 8);
      uint8_t v = (uint8_t)(180 * (1.0f - dist * 0.3f));
      leds[j] = CHSV(hue, 255, v);
    }
    if (cfgSparks && random8() < scale8(40, cfgSparks)) leds[random16(NUM_LEDS)] = CHSV(random8(), 200, scale8(200, cfgSparks));

  } else if (elapsed < 6000) {
    // Phase 2 (3-6s): wilder Party-Regenbogen ueber alles, schnell rotierend
    for (int i = 0; i < NUM_LEDS; i++) {
      uint8_t hue = (uint8_t)(i * 6 + now / 4);
      uint8_t shimmer = sin8((uint8_t)(i * 10 + now / 3));
      uint8_t v = 100 + scale8(shimmer, 80);
      leds[i] = CHSV(hue, 255, v);
    }
    if (cfgSparks && random8() < scale8(50, cfgSparks)) leds[random16(NUM_LEDS)] = CHSV(random8(), 180, scale8(220, cfgSparks));

  } else if (elapsed < 9000) {
    // Phase 3 (6-9s): Farben drehen sich langsam Richtung gruen
    float blend = (float)(elapsed - 6000) / 3000.0f;
    for (int i = 0; i < NUM_LEDS; i++) {
      uint8_t partyHue = (uint8_t)(i * 6 + now / 6);
      uint8_t hue = partyHue + (uint8_t)((96 - partyHue) * blend);
      uint8_t shimmer = sin8((uint8_t)(i * 8 + now / 5));
      uint8_t v = 100 + scale8(shimmer, 60);
      leds[i] = CHSV(hue, 255, v);
    }
    if (cfgSparks && random8() < scale8(20, cfgSparks)) leds[random16(NUM_LEDS)] = CHSV(40, 180, scale8(160, cfgSparks));

  } else if (elapsed < 12000) {
    // Phase 4 (9-12s): gruener Balken baut sich von links nach rechts auf
    float progress = (float)(elapsed - 9000) / 3000.0f;
    int fillEnd = (int)(progress * (NUM_LEDS - 1));
    for (int i = 0; i <= fillEnd; i++) {
      float ledPct = (float)i / (NUM_LEDS - 1);
      uint8_t sat = (uint8_t)(ledPct * 255);        // weiss -> gruen
      uint8_t v = 140;
      if (i == fillEnd) {
        uint8_t pulse = sin8((uint8_t)(now / 4));
        v = 100 + scale8(pulse, 80);
      }
      leds[i] = CHSV(96, sat, v);
    }
    if (cfgSparks && random8() < scale8(15, cfgSparks)) leds[random16(fillEnd + 1)] += CHSV(80, 200, scale8(60, cfgSparks));

  } else {
    // Phase 5 (12-15s): volles Gruen shimmernd, sanft ausklingen
    float fade = (float)(elapsed - 12000) / 3000.0f;
    if (fade > 1.0f) fade = 1.0f;
    for (int i = 0; i < NUM_LEDS; i++) {
      uint8_t shimmer = sin8((uint8_t)(i * 8 + now / 6));
      uint8_t full = 130 + scale8(shimmer, 50);
      uint8_t target = 90;
      uint8_t v = full - (uint8_t)((full - target) * fade);
      leds[i] = CHSV(96, 255, v);
    }
  }
}

// ----- MIDI-Parser: fuettert HA-State UND LED -----
void processMidi(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t hi = status & 0xF0;
  if (hi == 0x90 && d2 > 0) {                       // Note On
    if (!notes[d1].down) activeCount++;
    notes[d1].down = true; notes[d1].vel = d2;
    lastNoteNum = d1; lastNote = noteName(d1); lastVelocity = d2;
    if (!melPlaying) lastNoteMs = millis();
    notesToday++;
    if (!recPlaying && !melPlaying) notesPlayed++;
    if (!recPlaying) {
      JsonDocument ev; ev["note"]=lastNote; ev["num"]=d1; ev["vel"]=d2; ev["on"]=true;
      String p; serializeJson(ev,p); mqtt.publish(T_EVENT.c_str(), p.c_str());
    }
    if (d1 == 21)  { lowKeyDownMs = millis(); lowKeyFired = false; }
    if (d1 == 108) { highKeyDownMs = millis(); highKeyFired = false; }
    ledOnNote(d1, d2);
    dirty = true;
  } else if (hi == 0x80 || (hi == 0x90 && d2 == 0)) { // Note Off
    if (d1 == 21)  lowKeyDownMs = 0;
    if (d1 == 108) highKeyDownMs = 0;
    if (notes[d1].down) { notes[d1].down = false; if (activeCount>0) activeCount--; }
    dirty = true;
  } else if (hi == 0xB0) {                            // Control Change
    switch (d1) {
      case 64: sustainPos=d2; sustain=(d2>=64); dirty=true; break;
      case 66: sostenutoPos=d2; sostenuto=(d2>=64); dirty=true; break;
      case 67: softPos=d2; soft=(d2>=64); dirty=true; break;
    }
  } else if (hi == 0xC0) { lastProgram = d1; dirty = true; }  // Program Change
}

// ----- Vorspiel: lokaler Noten-Scheduler (praezises Timing, Velocity, Humanisierung) -----
struct MNote { uint8_t midi; float beats; };   // midi 0 = Pause
const MNote MEL_HAENSCHEN[] = {                 // Haenschen klein (ganzes Lied)
  {67,1},{64,1},{64,2}, {65,1},{62,1},{62,2}, {60,1},{62,1},{64,1},{65,1}, {67,1},{67,1},{67,2},
  {67,1},{64,1},{64,2}, {65,1},{62,1},{62,2}, {60,1},{64,1},{67,1},{67,1}, {72,2}
};
const MNote MEL_ENTCHEN[] = {                   // Alle meine Entchen (beide Zeilen)
  {60,1},{62,1},{64,1},{65,1}, {67,2},{67,2}, {69,1},{69,1},{69,1},{69,1}, {67,4},
  {67,1},{67,1},{67,1},{67,1}, {65,2},{65,2}, {64,1},{64,1},{64,1},{64,1}, {62,4}
};
const MNote MEL_COIN[] = {                      // Mario Coin hoch: B6-E7
  {95, 0.5f}, {100, 1.5f}
};
const MNote MEL_FANFARE[] = {                   // Triumph: G5-B5-D6-G6-pause-D6-G6
  {79, 0.4f}, {83, 0.4f}, {86, 0.4f}, {91, 0.8f}, {0, 0.3f}, {86, 0.3f}, {91, 1.5f}
};
// Ruf-Melodien (kurze + laengere "Komm spielen!"-Erinnerungen)
const MNote MEL_CUCKOO[] = {                    // Kuckuck: G-E
  {67, 0.8f}, {64, 1.5f}
};
const MNote MEL_DINGDONG[] = {                  // Tuerklingel: E-C
  {76, 0.8f}, {72, 1.5f}
};
const MNote MEL_QUESTION[] = {                  // Aufsteigende Quarte: C-F
  {72, 0.8f}, {77, 1.5f}
};
const MNote MEL_TRIAD[] = {                     // Dreiklang-Frage: C-E-G
  {72, 0.6f}, {76, 0.6f}, {79, 1.2f}
};
const MNote MEL_PLAYBOX[] = {                   // Spieluhr: G-E-G
  {79, 0.6f}, {76, 0.6f}, {79, 1.2f}
};
const MNote MEL_LONGING[] = {                   // Sehnsucht: C-E-G-A-G (aufsteigend, offen)
  {60, 0.8f}, {64, 0.6f}, {67, 0.6f}, {69, 1.0f}, {67, 1.5f}
};
const MNote MEL_DREAMER[] = {                   // Traeumer: E-G-A-G-E-D-E (sanft wiegend)
  {64, 0.6f}, {67, 0.5f}, {69, 0.8f}, {67, 0.5f}, {64, 0.5f}, {62, 0.5f}, {64, 1.5f}
};
const MNote MEL_SUNRISE[] = {                   // Sonnenaufgang: C-D-E-G-C' (aufwaerts, hell)
  {60, 0.5f}, {62, 0.5f}, {64, 0.5f}, {67, 0.7f}, {72, 1.8f}
};
const MNote MEL_WALTZ[] = {                     // Kleiner Walzer: G-B-D'-B-G-A-B (3/4-Feeling)
  {67, 0.8f}, {71, 0.5f}, {74, 0.8f}, {71, 0.5f}, {67, 0.5f}, {69, 0.5f}, {71, 1.5f}
};
const MNote MEL_LULLABY[] = {                   // Wiegenlied-Ruf: E-D-C-D-E-E-E (Marys Lamb rueckwaerts)
  {64, 0.6f}, {62, 0.6f}, {60, 0.6f}, {62, 0.6f}, {64, 0.6f}, {64, 0.6f}, {64, 1.2f}
};
struct CallMel { const MNote* data; int len; int tempo; };
const CallMel CALL_MELS[] = {
  {MEL_CUCKOO,   sizeof(MEL_CUCKOO)/sizeof(MNote),   400},
  {MEL_DINGDONG,  sizeof(MEL_DINGDONG)/sizeof(MNote), 400},
  {MEL_QUESTION,  sizeof(MEL_QUESTION)/sizeof(MNote), 400},
  {MEL_TRIAD,     sizeof(MEL_TRIAD)/sizeof(MNote),    350},
  {MEL_PLAYBOX,   sizeof(MEL_PLAYBOX)/sizeof(MNote),   380},
  {MEL_LONGING,   sizeof(MEL_LONGING)/sizeof(MNote),   450},
  {MEL_DREAMER,   sizeof(MEL_DREAMER)/sizeof(MNote),   420},
  {MEL_SUNRISE,   sizeof(MEL_SUNRISE)/sizeof(MNote),   380},
  {MEL_WALTZ,     sizeof(MEL_WALTZ)/sizeof(MNote),     400},
  {MEL_LULLABY,   sizeof(MEL_LULLABY)/sizeof(MNote),   350},
};
const int NUM_CALL_MELS = sizeof(CALL_MELS) / sizeof(CallMel);
const int HAENSCHEN_LEN = sizeof(MEL_HAENSCHEN) / sizeof(MNote);
const int ENTCHEN_LEN   = sizeof(MEL_ENTCHEN)   / sizeof(MNote);
const int COIN_LEN      = sizeof(MEL_COIN)      / sizeof(MNote);
const int FANFARE_LEN   = sizeof(MEL_FANFARE)   / sizeof(MNote);

const MNote* melData = nullptr;
int          melLen = 0, melIdx = 0, melQuarterMs = 500;
uint8_t      melNote = 0;
uint32_t     melOffAt = 0, melOnAt = 0;
uint8_t melBaseVel = 72;

void startMelody(const MNote* mel, int len, int qMs, uint8_t vel = 72) {
  if (!usbHost.isConnected()) return;
  melData = mel; melLen = len; melIdx = 0; melQuarterMs = qMs;
  melBaseVel = vel;
  melNote = 0; melOffAt = 0; melOnAt = millis(); melPlaying = true;
}

void playerTask(uint32_t now) {
  if (!melPlaying) return;
  // faellige NoteOff (ans Klavier + lokal fuer LED/HA)
  if (melNote && (int32_t)(now - melOffAt) >= 0) {
    midiHandler.sendNoteOff(1, melNote, 0);
    processMidi(0x80, melNote, 0);
    melNote = 0;
  }
  // naechste Note faellig?
  if (melNote == 0 && (int32_t)(now - melOnAt) >= 0) {
    if (melIdx >= melLen) { melPlaying = false; return; }
    MNote n = melData[melIdx++];
    uint16_t durMs = (uint16_t)(n.beats * melQuarterMs);
    if (n.midi > 0) {
      int v = melBaseVel + (int)random(-8, 9);               // Humanisierung: Velocity
      if (v < 1) v = 1; if (v > 127) v = 127;
      midiHandler.sendNoteOn(1, n.midi, (uint8_t)v);         // ans Klavier
      processMidi(0x90, n.midi, (uint8_t)v);                 // lokal: LED + HA
      melNote  = n.midi;
      melOffAt = now + (uint32_t)(durMs * 0.88f);            // leichtes Detache
      melOnAt  = now + durMs + (int)random(-12, 13);         // Humanisierung: Timing
    } else {
      melOnAt = now + durMs;                                 // Pause
    }
  }
}

// ----- Aufnahme + Wiedergabe (MidiEvt-Struct kommt aus clair_de_lune.h) -----
const int REC_MAX = 6000;            // ~3000 Noten (NoteOn+Off); CC64-Pedal kann mitzaehlen
MidiEvt        recBuf[REC_MAX];      // eigene Aufnahme
const MidiEvt* playSrc = nullptr;    // Wiedergabe-Quelle: recBuf ODER CLAIR
int            playLen = 0;

void allNotesOff() {
  midiHandler.sendControlChange(1, 123, 0);          // All Notes Off ans Klavier
  for (int i = 0; i < 128; i++) notes[i].down = false;
  activeCount = 0; dirty = true;
}

// MIDI-Ereignis ans Klavier UND lokal (LED/HA); false wenn OUT-Endpunkt gerade belegt
bool emitMidi(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t hi = status & 0xF0; uint8_t ch = (status & 0x0F) + 1;
  bool ok = true;
  if      (hi == 0x90 && d2 > 0)              ok = midiHandler.sendNoteOn(ch, d1, d2);
  else if (hi == 0x80 || (hi == 0x90 && d2 == 0)) ok = midiHandler.sendNoteOff(ch, d1, 0);
  else if (hi == 0xB0)                         ok = midiHandler.sendControlChange(ch, d1, d2);
  else if (hi == 0xC0)                         ok = midiHandler.sendProgramChange(ch, d1);
  if (ok) processMidi(status, d1, d2);
  return ok;
}

void recordEvent(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t hi = status & 0xF0;
  if (!(hi==0x80 || hi==0x90 || hi==0xB0 || hi==0xC0)) return;
  if ((hi==0x90 || hi==0x80) && (d1==21 || d1==108)) return;     // Trigger-Tasten nicht aufnehmen
  if (recCount >= REC_MAX) { recording = false; return; }        // Puffer voll -> Stop
  recBuf[recCount++] = { millis() - recStartMs, status, d1, d2 };
}

// Wiedergabe aus beliebiger Event-Quelle starten (Aufnahme oder Debussy)
void startPlay(const MidiEvt* src, int len) {
  if (len < 1 || !usbHost.isConnected()) return;
  melPlaying = false; recording = false; allNotesOff();
  playSrc = src; playLen = len; recPlayIdx = 0; recPlayStartMs = millis(); recPlaying = true;
}

void recPlayTask(uint32_t now) {
  if (!recPlaying || !playSrc) return;
  uint32_t elapsed = now - recPlayStartMs;
  while (recPlayIdx < playLen && playSrc[recPlayIdx].t <= elapsed) {
    const MidiEvt &e = playSrc[recPlayIdx];
    if (!emitMidi(e.status, e.d1, e.d2)) break;    // OUT belegt -> naechster Loop (chord-sicher)
    recPlayIdx++;
  }
  if (recPlayIdx >= playLen) { recPlaying = false; allNotesOff(); }
}

void publishCfg() {   // aktuelle Reglerwerte an HA zurueck (retained)
  mqtt.publish((String(DEVICE_ID)+"/num/maxbright").c_str(), String(cfgMaxBright).c_str(), true);
  mqtt.publish((String(DEVICE_ID)+"/num/ambient").c_str(),   String(cfgAmbient).c_str(),   true);
  mqtt.publish((String(DEVICE_ID)+"/num/scanner").c_str(),   String(cfgScanner).c_str(),   true);
  mqtt.publish((String(DEVICE_ID)+"/num/goalmin").c_str(),  String(cfgGoalMin).c_str(),   true);
  mqtt.publish((String(DEVICE_ID)+"/num/callfrom").c_str(), String(cfgCallFrom).c_str(), true);
  mqtt.publish((String(DEVICE_ID)+"/num/callto").c_str(),   String(cfgCallTo).c_str(),   true);
  mqtt.publish((String(DEVICE_ID)+"/num/standbymin").c_str(),String(cfgStandbyMin).c_str(),true);
  mqtt.publish((String(DEVICE_ID)+"/num/callfreq").c_str(), String(cfgCallFreq).c_str(), true);
  mqtt.publish((String(DEVICE_ID)+"/num/sparks").c_str(),   String(cfgSparks).c_str(),   true);
  mqtt.publish((String(DEVICE_ID)+"/num/callsound").c_str(),String(cfgCallSound).c_str(),true);
  const char* mn = (cfgMode==1) ? "Classic" : (cfgMode==2) ? "Party" : "Bunt";
  mqtt.publish((String(DEVICE_ID)+"/num/mode").c_str(), mn, true);
  mqtt.publish((String(DEVICE_ID)+"/num/progressbar").c_str(), cfgProgressBar ? "ON" : "OFF", true);
}

// MQTT-Callback: .../play (Befehle) und .../set/<name> (Regler)
void onMqtt(char* topic, byte* payload, unsigned int len) {
  String t = topic;
  String msg; for (unsigned i = 0; i < len; i++) msg += (char)payload[i];
  msg.trim();
  if (t.endsWith("/play")) {
    msg.toLowerCase();
    if      (msg == "entchen")     startMelody(MEL_ENTCHEN,   ENTCHEN_LEN,   520);
    else if (msg.startsWith("ha")) startMelody(MEL_HAENSCHEN, HAENSCHEN_LEN, 500);
    else if (msg == "rec")     { melPlaying=false; recPlaying=false; allNotesOff(); recCount=0; recStartMs=millis(); recording=true; dirty=true; }
    else if (msg == "recstop") { recording=false; dirty=true; }
    else if (msg == "recplay") startPlay(recBuf, recCount);
    else if (msg == "clair" || msg == "debussy") startPlay(CLAIR, CLAIR_LEN);
    else if (msg == "nocturne" || msg == "chopin") startPlay(NOCTURNE, NOCTURNE_LEN);
    else if (msg == "comptine" || msg == "amelie") startPlay(COMPTINE, COMPTINE_LEN);
    else if (msg == "wgwh" || msg == "schlaflied" || msg == "maus") startPlay(WGWH, WGWH_LEN);
    else if (msg == "entertainer" || msg == "joplin") startPlay(ENTERTAINER, ENTERTAINER_LEN);
    else if (msg == "moonlight" || msg == "mondschein") startPlay(MOONLIGHT, MOONLIGHT_LEN);
    else if (msg == "stop")    { melPlaying=false; recPlaying=false; recording=false; if (melNote) midiHandler.sendNoteOff(1, melNote, 0); melNote=0; allNotesOff(); dirty=true; }
    else if (msg == "testadd10")    { practiceMs += 10UL*60*1000; dirty=true; prefs.putUInt("pm",practiceMs); if (!goalReached && cfgGoalMin>0 && practiceMs>=(uint32_t)cfgGoalMin*60000UL) { goalReached=true; goalCelebration=true; celebrationStart=millis(); prefs.putBool("gr",true); startMelody(MEL_FANFARE, FANFARE_LEN, 250); } }
    else if (msg == "testreset")    { practiceMs=0; goalReached=false; goalCelebration=false; lastMilestonePct=0; prevFillEnd=0; milestoneFlashUntil=0; dirty=true; prefs.putUInt("pm",0); prefs.putBool("gr",false); }
    else if (msg == "testcelebrate"){ goalCelebration=true; celebrationStart=millis(); }
    else if (msg == "testgreen")    { colorTestVal=CRGB(0,150,0);   colorTestUntil=millis()+10000; Serial.println("TEST GREEN: R=0 G=150 B=0"); }
    else if (msg == "testred")      { colorTestVal=CRGB(150,0,0);   colorTestUntil=millis()+10000; Serial.println("TEST RED: R=150 G=0 B=0"); }
    else if (msg == "testblue")     { colorTestVal=CRGB(0,0,150);   colorTestUntil=millis()+10000; Serial.println("TEST BLUE: R=0 G=0 B=150"); }
    else if (msg == "testwhite")    { colorTestVal=CRGB(150,150,150);colorTestUntil=millis()+10000; Serial.println("TEST WHITE"); }
    else if (msg == "testgoalgreen"){ goalReached=true; prefs.putBool("gr",true); goalCelebration=true; celebrationStart=millis(); dirty=true; startMelody(MEL_FANFARE, FANFARE_LEN, 250); }
    else if (msg == "testcoin")     { startMelody(MEL_COIN, COIN_LEN, 180, 38); }
    else if (msg == "testfanfare")  { startMelody(MEL_FANFARE, FANFARE_LEN, 250); }
    else if (msg == "testcall")     { const CallMel&m=CALL_MELS[callSoundIdx]; startMelody(m.data, m.len, m.tempo, 35); wakeupShowUntil=millis()+8000; standby=false; callSoundIdx=(callSoundIdx+1)%NUM_CALL_MELS; }
    else if (msg == "testcallprev") { callSoundIdx=(callSoundIdx+NUM_CALL_MELS-1)%NUM_CALL_MELS; const CallMel&m=CALL_MELS[callSoundIdx]; startMelody(m.data, m.len, m.tempo, 35); wakeupShowUntil=millis()+8000; standby=false; }
    return;
  }
  if (t.endsWith("/progressbar")) {
    cfgProgressBar = (msg == "ON"); prefs.putBool("pb", cfgProgressBar); publishCfg(); return;
  }
  if (t.endsWith("/mode")) {     // Dropdown: "Bunt" | "Classic" | "Party"
    cfgMode = (msg == "Classic") ? 1 : (msg == "Party") ? 2 : 0;
    prefs.putUChar("md", cfgMode); publishCfg(); return;
  }
  int v = msg.toInt(); if (v < 0) v = 0; if (v > 255) v = 255;
  if      (t.endsWith("/maxbright")) { cfgMaxBright = v; FastLED.setBrightness(cfgMaxBright); prefs.putUChar("mb", v); }
  else if (t.endsWith("/ambient"))   { cfgAmbient   = v; prefs.putUChar("am", v); }
  else if (t.endsWith("/scanner"))   { cfgScanner   = v; prefs.putUChar("sc", v); }
  else if (t.endsWith("/goalmin"))  { cfgGoalMin   = v; prefs.putUChar("gm", v); }
  else if (t.endsWith("/callfrom"))  { cfgCallFrom  = v; prefs.putUChar("cf", v); }
  else if (t.endsWith("/callto"))    { cfgCallTo    = v; prefs.putUChar("ct", v); }
  else if (t.endsWith("/standbymin")){ cfgStandbyMin= max(1,v); prefs.putUChar("sm", cfgStandbyMin); }
  else if (t.endsWith("/callfreq"))  { cfgCallFreq  = constrain(v,1,5); prefs.putUChar("cq", cfgCallFreq); }
  else if (t.endsWith("/sparks"))    { cfgSparks    = v; prefs.putUChar("sp", v); }
  else if (t.endsWith("/callsound")){ cfgCallSound = constrain(v,0,5); prefs.putUChar("cs", cfgCallSound); }
  else return;
  publishCfg();
}

// ----- WLAN / MQTT (nicht-blockierend) -----
void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("kawai-klavier");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}
void setupOTA() {
  ArduinoOTA.setHostname("kawai-klavier");
  ArduinoOTA.begin();
}
uint32_t lastMqttTry = 0;
void ensureMqtt() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastMqttTry < 2000) return;
  lastMqttTry = millis();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);
  String cid = String(DEVICE_ID) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = (strlen(MQTT_USER) > 0)
    ? mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS, T_AVAIL.c_str(), 0, true, "offline")
    : mqtt.connect(cid.c_str(), nullptr, nullptr, T_AVAIL.c_str(), 0, true, "offline");
  if (ok) {
    mqtt.publish(T_AVAIL.c_str(), "online", true);
    mqtt.subscribe((String(DEVICE_ID) + "/play").c_str());    // Vorspiel-Befehle
    mqtt.subscribe((String(DEVICE_ID) + "/set/+").c_str());   // LED-Regler
    publishDiscovery(); publishState(true); publishCfg();
  }
}

// ----- Setup / Loop -----
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  prefs.begin("kawai", false);
  cfgMaxBright = prefs.getUChar("mb", MAX_BRIGHT);
  cfgAmbient   = prefs.getUChar("am", AMBIENT_MAX);
  cfgScanner   = prefs.getUChar("sc", 120);
  cfgMode      = prefs.getUChar("md", 0);
  cfgGoalMin   = prefs.getUChar("gm", 30);
  cfgCallFrom  = prefs.getUChar("cf", 14);
  cfgCallTo    = prefs.getUChar("ct", 20);
  cfgStandbyMin= prefs.getUChar("sm", 15);
  cfgCallFreq  = prefs.getUChar("cq", 3);
  cfgSparks    = prefs.getUChar("sp", 0);
  cfgCallSound = prefs.getUChar("cs", 3);
  cfgProgressBar = prefs.getBool("pb", true);
  practiceMs   = prefs.getUInt("pm", 0);
  goalReached  = prefs.getBool("gr", false);
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
  mqtt.setCallback(onMqtt);
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(UncorrectedColor).setRgbw(RgbwDefault());
  FastLED.setBrightness(cfgMaxBright);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 1600);   // dem 1,5-A-Netzteil etwas mehr zugetraut
  setupWifi();
  setupOTA();
  midiHandler.addTransport(&usbHost);
  usbHost.begin();
  midiHandler.begin();
  Serial.println("=== Kawai-Bruecke + Klaviatur-Licht bereit ===");
}

void loop() {
  ArduinoOTA.handle();
  ensureMqtt();
  mqtt.loop();

  midiHandler.task();
  for (const auto& ev : midiHandler.getQueue()) {
    uint8_t status = (uint8_t)ev.statusCode | ev.channel0;
    if (recording) recordEvent(status, ev.noteNumber, ev.velocity7);   // nur das selbst Gespielte
    processMidi(status, ev.noteNumber, ev.velocity7);
  }
  midiHandler.clearQueue();

  uint32_t now = millis();
  playerTask(now);
  recPlayTask(now);

  // Langdruck-Erkennung: A0 gehalten -> Aufnahme starten, C8 gehalten -> Stop + Abspielen
  if (lowKeyDownMs && !lowKeyFired && (now - lowKeyDownMs >= LONGPRESS_MS)) {
    lowKeyFired = true;
    melPlaying = false; recPlaying = false; allNotesOff();
    recCount = 0; recStartMs = now; recording = true; dirty = true;
    startMelody(MEL_COIN, COIN_LEN, 150, 50);
  }
  if (highKeyDownMs && !highKeyFired && (now - highKeyDownMs >= LONGPRESS_MS)) {
    highKeyFired = true;
    if (recording) {
      recording = false; dirty = true;
      startMelody(MEL_DINGDONG, sizeof(MEL_DINGDONG)/sizeof(MNote), 300, 50);
      // Wiedergabe nach kurzem Delay starten (Melodie ist ~0.7s)
    } else if (recCount > 0) {
      startPlay(recBuf, recCount);
    }
  }
  bool nowPlaying = (activeCount > 0) || (now - lastNoteMs < 1200);
  if (nowPlaying != playing) { playing = nowPlaying; dirty = true; }
  bool nowConn = usbHost.isConnected();
  if (nowConn != connectedPiano) { connectedPiano = nowConn; dirty = true; }
  publishState();

  // Spielzeit zaehlen: nur echte Tastenanschlaege (nicht Wiedergabe)
  if (activeCount > 0 && !recPlaying && !melPlaying) {
    if (lastPlayingMs > 0) {
      uint32_t delta = now - lastPlayingMs;
      if (delta < 3000) practiceMs += delta;    // Luecken >3s nicht mitzaehlen
    }
    lastPlayingMs = now;
    if (!goalReached && cfgGoalMin > 0 && practiceMs >= (uint32_t)cfgGoalMin * 60000UL) {
      goalReached = true; goalCelebration = true; celebrationStart = now; dirty = true;
      startMelody(MEL_FANFARE, FANFARE_LEN, 250);
    }
  } else {
    lastPlayingMs = 0;
  }

  // Spielzeit periodisch in NVS sichern (ueberlebt Reboot/OTA)
  if (practiceMs > 0 && now - lastPracticeSaveMs > PRACTICE_SAVE_INTERVAL) {
    prefs.putUInt("pm", practiceMs);
    prefs.putBool("gr", goalReached);
    lastPracticeSaveMs = now;
  }

  // Meilenstein-Check (10%, 20%, ... 90%) -> Flash ausloesen
  if (!goalReached && cfgGoalMin > 0 && activeCount > 0) {
    uint8_t curPct = (uint8_t)(practiceMs * 100 / ((uint32_t)cfgGoalMin * 60000UL));
    uint8_t curMile = (curPct / 10) * 10;
    if (curMile > lastMilestonePct && curMile >= 10 && curMile <= 90) {
      lastMilestonePct = curMile;
      milestoneFlashStart = now;
      milestoneFlashUntil = now + MILESTONE_FLASH_MS;
      if (curMile == 20 || curMile == 40 || curMile == 60 || curMile == 80) {
        startMelody(MEL_COIN, COIN_LEN, 180, 38);
      }
    }
  }

  // ----- Standby / Aufwach-Logik -----
  uint32_t standbyTimeoutMs = (uint32_t)cfgStandbyMin * 60000UL;
  bool idle = (activeCount == 0) && (now - lastNoteMs > IDLE_TIMEOUT_MS);
  bool deepIdle = idle && (now - lastNoteMs > standbyTimeoutMs);

  // Uhrzeit im Aufforderungs-Fenster?
  int hr = currentHour();
  bool inCallWindow = (hr >= 0) && (cfgCallFrom <= cfgCallTo
      ? (hr >= cfgCallFrom && hr < cfgCallTo)
      : (hr >= cfgCallFrom || hr < cfgCallTo));

  bool shouldCall = inCallWindow && !goalReached && cfgGoalMin > 0;

  // Aufwach-Intervall nach cfgCallFreq: 1=selten(8-12min) 5=oft(1-2min)
  // Formel: baseMs = 480000 - (freq-1)*100000 -> freq1=480s, freq5=80s; +random(halfBase)
  uint32_t callBaseMs = 480000UL - (uint32_t)(cfgCallFreq - 1) * 100000UL;
  uint32_t callRandMs = callBaseMs / 2;
  uint32_t quietBaseMs = 300000UL + random(300000);  // 5-10 Min

  if (!idle) {
    standby = false;
  } else if (deepIdle && !standby) {
    standby = true; standbyEnteredMs = now;
    uint32_t interval = shouldCall ? (callBaseMs + random(callRandMs)) : quietBaseMs;
    nextWakeupMs = now + interval;
    wakeupShowUntil = 0;
  }

  // Aufwachen aus Standby?
  if (standby && now >= nextWakeupMs && wakeupShowUntil == 0) {
    bool wakeup = shouldCall || (goalReached && random8() < 40);
    if (wakeup) {
      wakeupShowUntil = now + WAKEUP_SHOW_MS;
    }
    uint32_t interval = shouldCall ? (callBaseMs + random(callRandMs)) : quietBaseMs;
    nextWakeupMs = now + interval;
  }

  // Audio-Erinnerung (erst nach Standby-Timeout, dann periodisch)
  static uint32_t nextCallSoundMs = 0;
  if (!deepIdle) {
    nextCallSoundMs = 0;   // Reset: wird beim naechsten deepIdle neu geplant
  } else if (nextCallSoundMs == 0) {
    // Gerade erst in deepIdle eingetreten: erste Erinnerung planen
    uint32_t intervalMs = (660000UL - (uint32_t)(cfgCallSound - 1) * 120000UL);
    nextCallSoundMs = now + intervalMs / 2;
  } else if (shouldCall && cfgCallSound > 0 && now >= nextCallSoundMs && !melPlaying) {
    uint32_t intervalMs = (660000UL - (uint32_t)(cfgCallSound - 1) * 120000UL);
    const CallMel&m = CALL_MELS[callSoundIdx];
    startMelody(m.data, m.len, m.tempo, 35);
    callSoundIdx = (callSoundIdx + 1) % NUM_CALL_MELS;
    nextCallSoundMs = now + intervalMs + random(intervalMs / 4);
    wakeupShowUntil = now + WAKEUP_SHOW_MS;
  }

  bool showingWakeup = standby && wakeupShowUntil > 0 && now < wakeupShowUntil;
  if (standby && wakeupShowUntil > 0 && now >= wakeupShowUntil) {
    wakeupShowUntil = 0;   // Aufwach-Show vorbei
  }

  // ----- LED-Frame ~60 fps -----
  if (now - lastFrame >= 16) {
    if (colorTestUntil && now < colorTestUntil) {
      for (int i = 0; i < NUM_LEDS; i++) leds[i] = colorTestVal;
      FastLED.show(); lastFrame = now;
      static uint32_t lastDbg = 0;
      if (now - lastDbg > 2000) { lastDbg = now; Serial.printf("LED[0] R=%d G=%d B=%d\n", leds[0].r, leds[0].g, leds[0].b); }
    } else {
    colorTestUntil = 0;
    float dt = (now - lastFrame) / 1000.0f; lastFrame = now;
    if (goalCelebration && (now - celebrationStart < CELEBRATION_MS)) {
      renderCelebration(now, celebrationStart);
    } else {
      if (goalCelebration) goalCelebration = false;
      if (standby && !showingWakeup) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
      } else if (idle) {
        renderKnightRider(now, goalReached);
      } else {
        renderKeyboard(now, dt);
      }
    }
    FastLED.show();
    } // else (normales Rendering)
  } // if (lastFrame)

  // Tagesreset um Mitternacht (NTP-basiert, ueberlebt Reboot)
  {
    struct tm ti;
    if (getLocalTime(&ti, 0)) {
      uint16_t today = (ti.tm_year << 9) | ti.tm_yday;
      uint16_t savedDay = prefs.getUShort("pd", 0);
      if (savedDay != 0 && savedDay != today) {
        notesToday = 0; notesPlayed = 0; practiceMs = 0; goalReached = false;
        lastMilestonePct = 0; prevFillEnd = 0; milestoneFlashUntil = 0; dirty = true;
        prefs.putUInt("pm", 0); prefs.putBool("gr", false);
      }
      if (savedDay != today) prefs.putUShort("pd", today);
    }
  }
}
