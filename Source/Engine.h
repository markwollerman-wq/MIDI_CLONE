#pragma once
// MIDI Clone engine: analyses a MIDI clip and generates new clips that use the
// same notes in the same proportions, with the same rhythmic feel.
// Plain C++ (no JUCE), so it can be tested on its own.

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mc
{
struct Note
{
    double start  = 0.0;   // in beats (quarter notes) from the clip start
    double length = 0.25;  // in beats
    int pitch     = 60;
    int velocity  = 100;
};

// Order matches the MIDI Type menu (new types are added at the end so saved sets still load)
enum class MidiType   { bass = 0, melody = 1, arp = 2, stab = 3, chords = 4 };
enum class RhythmMode { locked = 0, sameFeel = 1 };

struct Settings
{
    MidiType type       = MidiType::bass;
    RhythmMode rhythm   = RhythmMode::locked;
    bool slide          = false;   // false = Grid Snapped
    float slideAmount   = 0.25f;   // 0..1, share of notes that glide into the next
    int variation       = 5;       // 1..10
    bool keepShape      = false;   // clones follow the source's up/down movement
    float newNotes      = 0.0f;    // 0..0.3, share of notes replaced by new in-key notes
    int keyRoot         = 0;       // key used for New Notes
    int keyScale        = -1;      // index into scales(), -1 = unknown (New Notes off)
    bool matchTime      = false;   // match the percentages by playing time instead of note count
    double maxSourceOverlap = -1.0; // how much a clone may share with the source (-1 = automatic)
    int referenceLane   = -1;      // More like this: the lane the others are based on (no "different from" check)
};

// A note on the 16th-note step grid
struct Step
{
    int step   = 0;   // 16th-note index
    int length = 1;   // in 16ths
    int pitch  = 0;
};

struct PitchShare
{
    int pitch   = 0;
    int count   = 0;
    double share = 0.0;       // 0..1 of all notes
    double timeShare = 0.0;   // 0..1 of all playing time
};

// A chord (or single note) starting on one step. Its identity is the set of
// pitch classes as a 12-bit mask, so C minor in any voicing is the same chord.
struct ChordEvent
{
    int step = 0, length = 1;
    std::vector<int> pitches;   // sorted, no duplicates
    int velocity = 100;
    int mask = 0;
};

struct ChordShare
{
    int mask = 0;
    int count = 0;
    double share = 0.0;
    std::string name;
};

struct Analysis
{
    bool valid = false;
    std::string problem;

    std::vector<Note> notes;          // one note at a time (lowest of any chord), starting at bar 1
    double offsetBeats = 0.0;         // empty bars removed from the start of the file
    bool hadChords = false;
    int beatsPerBar = 4;
    int bars = 1;
    double lengthBeats = 4.0;
    int stepsPerBar = 16;

    int home = 0;                     // home note: most "landed on" (time, beat 1, first/last, lowest)
    int lowest = 0, highest = 0;
    std::vector<PitchShare> shares;   // most common first

    int periodBars = 1;               // length of the repeating phrase
    std::vector<Step> phrase;         // source rhythm + notes of the first phrase
    std::vector<Step> allSteps;       // the whole clip on the step grid
    std::vector<int> barGroup;        // bars with identical notes + rhythm share a group number
    std::map<int, std::map<int, int>> transitions;   // pitch -> next pitch -> count
    double gapBeats = 0.06;           // typical silence at the end of each note
    int velocity = 100;

    // Chords (filled for every clip; used by the Chords type)
    std::vector<Note> polyNotes;               // every note, starting at bar 1
    std::vector<ChordEvent> chords;            // whole clip
    std::vector<ChordEvent> chordPhrase;       // first repeating chord phrase
    int chordPeriodBars = 1;
    std::vector<ChordShare> chordShares;       // most common first
    std::map<int, std::vector<int>> voicing;   // chord mask -> how the source voiced it
    std::map<int, std::map<int, int>> chordTransitions;
    int homeChord = 0;                         // mask of the home chord
    double chordRatio = 0.0;                   // share of onsets with 2+ notes
    double chordGap = 0.06;

    MidiType guessedType = MidiType::bass;
};

struct Lane
{
    std::vector<Note> notes;
    double overlapWithSource = 0.0;   // share of notes on the same step + pitch as the source
    int newNoteCount = 0;             // notes not in the source (New Notes)
    bool locked = false;
    bool empty = true;
};

constexpr int numLanes = 10;
using LaneSet = std::array<Lane, numLanes>;

// homePitchClass: use this as the home note when it's in the clip (-1 = work it out)
Analysis analyse (std::vector<Note> notes, int beatsPerBar, int homePitchClass = -1);

// Regenerates every unlocked lane. Locked lanes are kept as they are.
void generate (const Analysis& a, const Settings& s, LaneSet& lanes, std::uint32_t seed);

// More like this: every unlocked lane (except ref) becomes the reference lane with a few notes or
// chords swapped (more swaps with higher Variation). Rhythm and percentages stay the same.
void varyFrom (int ref, const Settings& s, LaneSet& lanes, std::uint32_t seed);

// Defaults each MIDI Type starts with
RhythmMode defaultRhythmFor (MidiType t);
bool defaultKeepShapeFor (MidiType t);

std::string chordName (int mask, int bassPitchClass = -1);   // "Cm", "Ab", "Bbsus4", ...
std::string noteName (int pitch);   // "D", "Eb", ...
std::string noteNameWithOctave (int pitch);   // Ableton style: middle C = C3
double overlap (const std::vector<Note>& a, const std::vector<Note>& b);
} // namespace mc
