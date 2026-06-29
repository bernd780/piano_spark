#pragma once
#include <stdint.h>
// Gemeinsamer Event-Typ fuer Aufnahme und alle eingebauten Stuecke (clair/nocturne/comptine).
struct MidiEvt { uint32_t t; uint8_t status, d1, d2; };
