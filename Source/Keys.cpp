#include "Keys.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace mc
{
namespace
{
int pc (int pitch) { return ((pitch % 12) + 12) % 12; }

const char* const rootNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

std::set<int> pitchSet (const Key& k)
{
    std::set<int> s;
    for (int iv : scales()[(size_t) k.scale].intervals) s.insert ((k.root + iv) % 12);
    return s;
}
} // namespace

const std::vector<Scale>& scales()
{
    static const std::vector<Scale> list = {
        { "Major",              { 0, 2, 4, 5, 7, 9, 11 }, 0 },
        { "Minor",              { 0, 2, 3, 5, 7, 8, 10 }, 1 },
        { "Dorian",             { 0, 2, 3, 5, 7, 9, 10 }, 2 },
        { "Phrygian",           { 0, 1, 3, 5, 7, 8, 10 }, 3 },
        { "Mixolydian",         { 0, 2, 4, 5, 7, 9, 10 }, 4 },
        { "Lydian",             { 0, 2, 4, 6, 7, 9, 11 }, 5 },
        { "Harmonic Minor",     { 0, 2, 3, 5, 7, 8, 11 }, 6 },
        { "Melodic Minor",      { 0, 2, 3, 5, 7, 9, 11 }, 7 },
        { "Phrygian Dominant",  { 0, 1, 4, 5, 7, 8, 10 }, 8 },
        { "Locrian",            { 0, 1, 3, 5, 6, 8, 10 }, 9 },
        { "Minor Pentatonic",   { 0, 3, 5, 7, 10 }, 10 },
        { "Major Pentatonic",   { 0, 2, 4, 7, 9 }, 11 },
        { "Blues",              { 0, 3, 5, 6, 7, 10 }, 12 },
        { "Major Blues",        { 0, 2, 3, 4, 7, 9 }, 13 },
        { "Hungarian Minor",    { 0, 2, 3, 6, 7, 8, 11 }, 14 },
        { "Double Harmonic",    { 0, 1, 4, 5, 7, 8, 11 }, 15 },
        { "Lydian Dominant",    { 0, 2, 4, 6, 7, 9, 10 }, 16 },
        { "Dorian b2",          { 0, 1, 3, 5, 7, 9, 10 }, 17 },
        { "Lydian Augmented",   { 0, 2, 4, 6, 8, 9, 11 }, 18 },
        { "Mixolydian b6",      { 0, 2, 4, 5, 7, 8, 10 }, 19 },
        { "Locrian #2",         { 0, 2, 3, 5, 6, 8, 10 }, 20 },
        { "Altered",            { 0, 1, 3, 4, 6, 8, 10 }, 21 },
        { "Harmonic Major",     { 0, 2, 4, 5, 7, 8, 11 }, 22 },
        { "Neapolitan Minor",   { 0, 1, 3, 5, 7, 8, 11 }, 23 },
        { "Neapolitan Major",   { 0, 1, 3, 5, 7, 9, 11 }, 24 },
        { "Hirajoshi",          { 0, 2, 3, 7, 8 }, 25 },
        { "In Sen",             { 0, 1, 5, 7, 10 }, 26 },
        { "Whole Tone",         { 0, 2, 4, 6, 8, 10 }, 27 },
        { "Diminished (W-H)",   { 0, 2, 3, 5, 6, 8, 9, 11 }, 28 },
        { "Diminished (H-W)",   { 0, 1, 3, 4, 6, 7, 9, 10 }, 29 },
        { "Chromatic",          { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 99 },
    };
    return list;
}

int chromaticScaleIndex() { return (int) scales().size() - 1; }

bool inKey (int pitch, const Key& k)
{
    if (! k.valid()) return true;
    const int rel = pc (pitch - k.root);
    const auto& iv = scales()[(size_t) k.scale].intervals;
    return std::find (iv.begin(), iv.end(), rel) != iv.end();
}

std::string keyName (const Key& k)
{
    if (! k.valid()) return "No key";
    if (k.scale == chromaticScaleIndex()) return "Chromatic";
    return std::string (rootNames[pc (k.root)]) + " " + scales()[(size_t) k.scale].name;
}

std::string sameNotesAs (const Key& k)
{
    if (! k.valid() || scales()[(size_t) k.scale].intervals.size() != 7) return {};
    const auto mine = pitchSet (k);
    std::vector<std::string> names;
    for (int scale : { 0, 1 })   // major, minor
        for (int r = 0; r < 12; ++r)
        {
            Key other { r, scale };
            if ((other.root == k.root && other.scale == k.scale) || pitchSet (other) != mine) continue;
            names.push_back (std::string (rootNames[r]) + (scale == 0 ? " major" : " minor"));
        }
    std::string out;
    for (size_t i = 0; i < names.size(); ++i) out += (i ? " / " : "") + names[i];
    return out;
}

std::map<int, double> homeWeights (const std::vector<Note>& notes, int beatsPerBar)
{
    std::map<int, double> w;
    if (notes.empty()) return w;
    const int stepsPerBar = std::max (1, beatsPerBar) * 4;
    int lowest = notes.front().pitch;
    for (auto& n : notes) lowest = std::min (lowest, n.pitch);

    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        const long step = std::lround (n.start * 4.0);
        // Cap each note's time at the gap to the next onset (overlaps don't count twice)
        double len = n.length;
        if (i + 1 < notes.size() && notes[i + 1].start > n.start)
            len = std::min (len, notes[i + 1].start - n.start);
        w[pc (n.pitch)] += len;
        if (step % stepsPerBar == 0) w[pc (n.pitch)] += 0.5;   // lands on beat 1
    }
    w[pc (notes.front().pitch)] += 0.5;
    w[pc (notes.back().pitch)] += 0.5;
    w[pc (lowest)] += 0.5;
    return w;
}

int countOffKey (const std::vector<Note>& notes, const Key& k)
{
    int off = 0;
    for (auto& n : notes) off += inKey (n.pitch, k) ? 0 : 1;
    return off;
}

KeyReport detectKey (const std::vector<Note>& notes, int beatsPerBar)
{
    KeyReport r;
    r.totalNotes = (int) notes.size();
    if (notes.empty()) return r;

    const auto w = homeWeights (notes, beatsPerBar);
    r.homePitchClass = std::max_element (w.begin(), w.end(), [] (auto& a, auto& b) { return a.second < b.second; })->first;

    // Cost = share of notes that would change, plus a small penalty for rarer scales and for
    // a root that isn't the clip's home note. So a common scale on the home note wins unless
    // another key is clearly better.
    struct Ranked { KeyCandidate c; double cost; int rank; };
    std::vector<Ranked> all;
    const double total = std::max<size_t> (1, notes.size());
    for (int s = 0; s < (int) scales().size(); ++s)
    {
        if (s == chromaticScaleIndex()) continue;   // would always "fit": manual only
        const auto& sc = scales()[(size_t) s];
        double rarity = 0.0;
        if (sc.rank >= 14)      rarity = 0.12;   // exotic
        else if (sc.rank >= 10) rarity = 0.06;   // pentatonic / blues
        else if (sc.rank >= 6)  rarity = 0.03;   // harmonic / melodic minor, Phrygian dominant, Locrian
        for (int root = 0; root < 12; ++root)
        {
            KeyCandidate c;
            c.key = { root, s };
            c.offKey = countOffKey (notes, c.key);
            auto it = w.find (root);
            c.homeWeight = it == w.end() ? 0.0 : it->second;
            double cost = c.offKey / total + rarity + (root == r.homePitchClass ? 0.0 : 0.10);
            // Scales with fewer notes fit too easily: only suggest them when they fit almost exactly.
            if (sc.intervals.size() < 7 && c.offKey > 1) cost += 10.0;
            // Eight-note scales also fit easily
            if (sc.intervals.size() > 7) cost += 0.05;
            all.push_back ({ c, cost, sc.rank });
        }
    }
    std::sort (all.begin(), all.end(), [] (const Ranked& a, const Ranked& b)
    {
        if (std::abs (a.cost - b.cost) > 1e-9) return a.cost < b.cost;
        if (std::abs (a.c.homeWeight - b.c.homeWeight) > 1e-9) return a.c.homeWeight > b.c.homeWeight;
        return a.rank < b.rank;
    });
    for (auto& x : all) r.ranked.push_back (x.c);
    return r;
}

FixResult fixToKey (const std::vector<Note>& notes, const Key& k)
{
    FixResult fr;
    fr.notes = notes;
    if (! k.valid()) return fr;

    // Nearest note in the previous / next onset (for chords, the closest in pitch)
    auto neighbour = [&] (size_t i, int dir) -> int
    {
        const double t = notes[i].start;
        int best = -1;
        double bestTime = dir < 0 ? -1e18 : 1e18;
        for (size_t j = 0; j < notes.size(); ++j)
        {
            const double tj = notes[j].start;
            const bool side = dir < 0 ? tj < t - 1e-6 : tj > t + 1e-6;
            if (! side) continue;
            const bool closerInTime = dir < 0 ? tj > bestTime + 1e-6 : tj < bestTime - 1e-6;
            const bool sameTime = std::abs (tj - bestTime) <= 1e-6;
            if (closerInTime || (sameTime && best >= 0
                                 && std::abs (notes[j].pitch - notes[i].pitch) < std::abs (best - notes[i].pitch)))
            {
                best = notes[j].pitch;
                bestTime = tj;
            }
        }
        return best;
    };

    for (size_t i = 0; i < notes.size(); ++i)
    {
        const int p = notes[i].pitch;
        if (inKey (p, k)) continue;
        fr.offKey.push_back ((int) i);

        const int prev = neighbour (i, -1), next = neighbour (i, +1);
        int dir = 0;
        if (prev >= 0) dir = (p > prev) - (p < prev);            // keep the direction it arrived from
        if (dir == 0 && next >= 0) dir = (next > p) - (next < p); // repeated note: head toward the next note
        if (dir == 0) dir = 1;

        int upDist = 1, downDist = 1;
        while (upDist < 12 && ! inKey (p + upDist, k)) ++upDist;
        while (downDist < 12 && ! inKey (p - downDist, k)) ++downDist;
        int dist = dir > 0 ? upDist : downDist;
        // Don't jump a big gap when the other way is only a half-step
        if (dist > 2 && (dir > 0 ? downDist : upDist) == 1) { dir = -dir; dist = 1; }

        fr.notes[i].pitch = p + dir * dist;
    }

    for (int i : fr.offKey)
    {
        const auto ui = (size_t) i;
        const bool repeatsPrev = ui > 0 && fr.notes[ui].pitch == fr.notes[ui - 1].pitch && notes[ui].pitch != notes[ui - 1].pitch;
        const bool repeatsNext = ui + 1 < notes.size() && fr.notes[ui].pitch == fr.notes[ui + 1].pitch && notes[ui].pitch != notes[ui + 1].pitch;
        if (repeatsPrev || repeatsNext) fr.repeats.push_back (i);
    }
    return fr;
}
} // namespace mc
