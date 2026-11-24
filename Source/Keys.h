#pragma once
// Key Check: finds the key a clip is meant to be in and fixes out-of-key notes.
// Plain C++ (no JUCE).

#include <map>
#include <string>
#include <vector>
#include "Engine.h"

namespace mc
{
struct Scale
{
    const char* name;
    std::vector<int> intervals;   // semitones above the root
    int rank;                     // lower = more common (wins ties)
};

const std::vector<Scale>& scales();
int chromaticScaleIndex();        // "Chromatic (no key)": every note is in key

struct Key
{
    int root  = 0;    // pitch class 0..11
    int scale = -1;   // index into scales(), -1 = none
    bool valid() const { return scale >= 0; }
};

bool inKey (int pitch, const Key& k);
std::string keyName (const Key& k);                  // "C Phrygian"
std::string sameNotesAs (const Key& k);              // "Ab major / F minor", empty if none

struct KeyCandidate
{
    Key key;
    int offKey = 0;          // notes that would change
    double homeWeight = 0.0;
};

struct KeyReport
{
    std::vector<KeyCandidate> ranked;   // best first (chromatic is never auto-picked)
    int totalNotes = 0;
    int homePitchClass = 0;
};

// How much each pitch class sounds like "home": sounding time, plus extra for
// landing on beat 1 of a bar, being the first or last note, and being the lowest note.
std::map<int, double> homeWeights (const std::vector<Note>& notes, int beatsPerBar);

KeyReport detectKey (const std::vector<Note>& notes, int beatsPerBar);
int countOffKey (const std::vector<Note>& notes, const Key& k);

struct FixResult
{
    std::vector<Note> notes;       // same order and timing as the input
    std::vector<int> offKey;       // indices of out-of-key notes in the input
    std::vector<int> repeats;      // fixed notes that now repeat a neighbour
};

// Moves each out-of-key note to the nearest in-key note, continuing the direction
// the line arrived from (climbing -> up, falling -> down). Nothing else changes.
// Input must be sorted by start time, then pitch.
FixResult fixToKey (const std::vector<Note>& notes, const Key& k);
} // namespace mc
