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

enum class MidiType   { bass = 0, melody = 1, arp = 2 };
enum class RhythmMode { locked = 0, sameFeel = 1 };

struct Settings
{
    MidiType type       = MidiType::bass;
    RhythmMode rhythm   = RhythmMode::locked;
    bool slide          = false;   // false = Grid Snapped
    float slideAmount   = 0.25f;   // 0..1, share of notes that glide into the next
    int variation       = 5;       // 1..10
};

// A note on the 16th-note step grid
struct Step
{
    int step   = 0;   // 16th-note index inside the phrase
    int length = 1;   // in 16ths
    int pitch  = 0;
};

struct PitchShare
{
    int pitch   = 0;
    int count   = 0;
    double share = 0.0;   // 0..1 of all notes
};

struct Analysis
{
    bool valid = false;
    std::string problem;

    std::vector<Note> notes;          // one note at a time (lowest of any chord)
    bool hadChords = false;
    int beatsPerBar = 4;
    int bars = 1;
    double lengthBeats = 4.0;
    int stepsPerBar = 16;

    int home = 0;                     // most-played note (by sounding time)
    int lowest = 0, highest = 0;
    std::vector<PitchShare> shares;   // most common first

    int periodBars = 1;               // length of the repeating phrase
    std::vector<Step> phrase;         // source rhythm + notes of the first phrase
    std::map<int, std::map<int, int>> transitions;   // pitch -> next pitch -> count
    double gapBeats = 0.06;           // typical silence at the end of each note
    int velocity = 100;

    MidiType guessedType = MidiType::bass;
};

struct Lane
{
    std::vector<Note> notes;
    double overlapWithSource = 0.0;   // share of notes on the same step + pitch as the source
    bool locked = false;
    bool empty = true;
};

constexpr int numLanes = 10;
using LaneSet = std::array<Lane, numLanes>;

Analysis analyse (std::vector<Note> notes, int beatsPerBar);

// Regenerates every unlocked lane. Locked lanes are kept as they are.
void generate (const Analysis& a, const Settings& s, LaneSet& lanes, std::uint32_t seed);

// Rhythm mode each type starts with
RhythmMode defaultRhythmFor (MidiType t);

std::string noteName (int pitch);   // "D", "Eb", ...
std::string noteNameWithOctave (int pitch);   // Ableton style: middle C = C3
double overlap (const std::vector<Note>& a, const std::vector<Note>& b);
} // namespace mc
