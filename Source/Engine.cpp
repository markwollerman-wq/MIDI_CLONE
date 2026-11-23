#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <set>

namespace mc
{
namespace
{
using Rng = std::mt19937;

double uniform (Rng& r)                 { return std::uniform_real_distribution<double> (0.0, 1.0) (r); }
int randInt (Rng& r, int lo, int hi)    { return std::uniform_int_distribution<int> (lo, hi) (r); }   // inclusive

struct RawStep { int step, len, pitch, velocity; double origLength; };

// Rhythm as a token list: a value > 0 is a note that long (in 16ths), 0 is a one-16th rest
std::vector<int> tokensFromPhrase (const std::vector<Step>& phrase, int phraseSteps)
{
    std::vector<int> t;
    int pos = 0;
    for (auto& s : phrase)
    {
        while (pos < s.step) { t.push_back (0); ++pos; }
        t.push_back (s.length);
        pos += s.length;
    }
    while (pos < phraseSteps) { t.push_back (0); ++pos; }
    return t;
}

std::vector<std::pair<int, int>> stepsFromTokens (const std::vector<int>& tokens)
{
    std::vector<std::pair<int, int>> out;   // (step, length)
    int pos = 0;
    for (int t : tokens)
    {
        if (t > 0) { out.push_back ({ pos, t }); pos += t; }
        else       { ++pos; }
    }
    return out;
}

int countNotes (const std::vector<int>& tokens)
{
    return (int) std::count_if (tokens.begin(), tokens.end(), [] (int t) { return t > 0; });
}

std::vector<int> makeRhythm (const std::vector<int>& src, const Settings& s, Rng& rng)
{
    if (s.rhythm == RhythmMode::locked || src.size() < 3)
        return src;

    const bool mustStartOnNote = (s.type != MidiType::melody) && ! src.empty() && src.front() > 0;
    const int size = (int) src.size();

    for (int attempt = 0; attempt < 60; ++attempt)
    {
        auto t = src;
        if (s.variation >= 8)
        {
            std::shuffle (t.begin(), t.end(), rng);
        }
        else
        {
            // Move a few notes / rests; low variation keeps moves short
            const int moves = std::max (1, (int) std::lround (s.variation * size / 6.0));
            const int reach = s.variation <= 3 ? 2 : (s.variation <= 6 ? 4 : size);
            for (int m = 0; m < moves; ++m)
            {
                const int i = randInt (rng, 0, size - 1);
                const int v = t[(size_t) i];
                t.erase (t.begin() + i);
                const int j = std::clamp (i + randInt (rng, -reach, reach), 0, size - 1);
                t.insert (t.begin() + j, v);
            }
        }
        if (mustStartOnNote && t.front() == 0) continue;
        if (t == src && s.variation > 1 && attempt < 40) continue;
        return t;
    }
    return src;
}

// Note counts for one phrase, keeping the source's percentages (largest remainder rounding)
std::vector<int> buildBag (const Analysis& a, int notesInPhrase)
{
    const int total = std::accumulate (a.shares.begin(), a.shares.end(), 0,
                                       [] (int acc, const PitchShare& p) { return acc + p.count; });
    std::vector<int> counts;
    std::vector<std::pair<double, size_t>> remainders;
    int used = 0;
    for (size_t i = 0; i < a.shares.size(); ++i)
    {
        const double quota = (double) a.shares[i].count * notesInPhrase / std::max (1, total);
        const int c = (int) std::floor (quota);
        counts.push_back (c);
        used += c;
        remainders.push_back ({ quota - c, i });
    }
    std::stable_sort (remainders.begin(), remainders.end(),
                      [] (auto& x, auto& y) { return x.first > y.first; });
    for (size_t k = 0; used < notesInPhrase && k < remainders.size(); ++k, ++used)
        ++counts[remainders[k].second];

    std::vector<int> bag;
    for (size_t i = 0; i < counts.size(); ++i)
        for (int k = 0; k < counts[i]; ++k)
            bag.push_back (a.shares[i].pitch);
    return bag;
}

double transitionProb (const Analysis& a, int from, int to)
{
    auto it = a.transitions.find (from);
    if (it == a.transitions.end()) return 0.0;
    int total = 0;
    for (auto& kv : it->second) total += kv.second;
    auto jt = it->second.find (to);
    return (jt == it->second.end() || total == 0) ? 0.0 : (double) jt->second / total;
}

double scoreOrder (const std::vector<int>& seq, const std::vector<std::pair<int, int>>& steps,
                   const Analysis& a, const Settings& s)
{
    const int n = (int) seq.size();
    const double markovWeight = 1.5 * (11 - s.variation) / 10.0;
    const bool hasHighTier = a.highest != a.home;
    double score = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const int p  = seq[(size_t) i];
        const int st = steps[(size_t) i].first;
        const int len = steps[(size_t) i].second;
        const int prev = i > 0 ? seq[(size_t) i - 1] : -1;
        const int jump = prev >= 0 ? std::abs (p - prev) : 0;

        if (prev >= 0)
            score += markovWeight * transitionProb (a, prev, p) * (s.type == MidiType::arp ? 0.5 : 1.0);

        switch (s.type)
        {
            case MidiType::bass:
                if (st % 4 == 0 && p == a.home) score += 1.0;                  // home note on the beat
                if (hasHighTier && p == a.highest) score += (len <= 2 ? 1.2 : -0.8);   // high note = short hit
                if (i == 0 && std::abs (p - a.home) <= 2) score += 2.0;
                if (i == n - 1 && hasHighTier && p == a.highest) score -= 2.0;
                if (jump > 7) score -= 0.6;
                break;

            case MidiType::melody:
                score -= 0.12 * std::max (0, jump - 2);
                if (jump > 9) score -= 0.8;
                if (i == n - 1 && p == a.home) score += 2.0;                   // phrase resolves home
                if (st % 8 == 0 && p == a.home) score += 0.5;
                if (i >= 2 && p == prev && p == seq[(size_t) i - 2]) score -= 0.6;
                break;

            case MidiType::arp:
                if (p == prev) score -= 1.5;                                   // keep the pattern moving
                if (i >= 2)
                {
                    const int d1 = p - prev, d0 = prev - seq[(size_t) i - 2];
                    if ((d1 > 0 && d0 > 0) || (d1 < 0 && d0 < 0)) score += 0.35;
                }
                if (i == 0) score += (p == a.home ? 1.0 : (p == a.lowest ? 0.6 : 0.0));
                break;
        }
    }
    return score;
}

std::vector<int> assignPitches (std::vector<int> bag, const std::vector<std::pair<int, int>>& steps,
                                const Analysis& a, const Settings& s, Rng& rng, double chaos)
{
    std::shuffle (bag.begin(), bag.end(), rng);
    const int n = (int) bag.size();
    if (n < 2) return bag;
    // Short clips have few possible orders: when the best ones are used up, take a random order
    if (uniform (rng) < chaos) return bag;

    bool allSame = std::all_of (bag.begin(), bag.end(), [&] (int p) { return p == bag.front(); });
    if (allSame) return bag;

    auto cur = bag;
    double curScore = scoreOrder (cur, steps, a, s);
    auto best = cur;
    double bestScore = curScore;
    const double temperature = 0.04 + 0.05 * s.variation;
    const int iterations = 300 + 60 * n;

    for (int it = 0; it < iterations; ++it)
    {
        const int i = randInt (rng, 0, n - 1), j = randInt (rng, 0, n - 1);
        if (cur[(size_t) i] == cur[(size_t) j]) continue;
        std::swap (cur[(size_t) i], cur[(size_t) j]);
        const double sc = scoreOrder (cur, steps, a, s);
        if (sc >= curScore || uniform (rng) < std::exp ((sc - curScore) / temperature))
        {
            curScore = sc;
            if (sc > bestScore) { bestScore = sc; best = cur; }
        }
        else
        {
            std::swap (cur[(size_t) i], cur[(size_t) j]);
        }
    }
    // Higher variation: sometimes keep a less "perfect" but still good order
    return (uniform (rng) < s.variation / 20.0) ? cur : best;
}

std::vector<Note> buildClip (const Analysis& a, const Settings& s,
                             const std::vector<std::pair<int, int>>& steps,
                             const std::vector<int>& pitches, Rng& rng)
{
    const int phraseSteps = a.periodBars * a.stepsPerBar;
    const int repeats = std::max (1, a.bars / a.periodBars);
    std::vector<Step> all;

    for (int r = 0; r < repeats; ++r)
    {
        auto seq = pitches;
        // Second time through: swap two different notes (same counts, a little variation)
        if (repeats > 1 && r == repeats - 1 && s.type != MidiType::arp
            && uniform (rng) < s.variation / 10.0 && seq.size() > 1)
        {
            for (int tries = 0; tries < 20; ++tries)
            {
                const int i = randInt (rng, 0, (int) seq.size() - 1), j = randInt (rng, 0, (int) seq.size() - 1);
                if (seq[(size_t) i] != seq[(size_t) j]) { std::swap (seq[(size_t) i], seq[(size_t) j]); break; }
            }
        }
        for (size_t k = 0; k < steps.size(); ++k)
            all.push_back ({ r * phraseSteps + steps[k].first, steps[k].second, seq[k] });
    }

    std::vector<Note> notes;
    for (size_t k = 0; k < all.size(); ++k)
    {
        const auto& st = all[k];
        const double nominal = st.length * 0.25;
        double gap = a.gapBeats;
        if (s.type == MidiType::arp) gap = std::max (gap, nominal * 0.25);
        double len = std::max (nominal - gap, nominal * 0.5);

        if (s.slide && k + 1 < all.size())
        {
            const auto& next = all[k + 1];
            if (next.step == st.step + st.length && next.pitch != st.pitch && uniform (rng) < s.slideAmount)
                len = nominal + 0.03125;   // overlap the next note so the synth glides into it
        }

        int vel = a.velocity;
        if (s.type != MidiType::bass)
            vel += (st.step % 4 == 0) ? 8 : (st.step % 2 == 1 ? -6 : 0);

        notes.push_back ({ st.step * 0.25, len, st.pitch, std::clamp (vel, 1, 127) });
    }
    return notes;
}
} // namespace

std::string noteName (int pitch)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[((pitch % 12) + 12) % 12];
}

std::string noteNameWithOctave (int pitch)
{
    return noteName (pitch) + std::to_string (pitch / 12 - 2);
}

RhythmMode defaultRhythmFor (MidiType t)
{
    return t == MidiType::melody ? RhythmMode::sameFeel : RhythmMode::locked;
}

double overlap (const std::vector<Note>& a, const std::vector<Note>& b)
{
    if (a.empty() || b.empty()) return 0.0;
    std::set<std::pair<long, int>> sa;
    for (auto& n : a) sa.insert ({ std::lround (n.start * 4.0), n.pitch });
    int match = 0;
    for (auto& n : b) match += sa.count ({ std::lround (n.start * 4.0), n.pitch }) ? 1 : 0;
    return (double) match / (double) std::max (a.size(), b.size());
}

Analysis analyse (std::vector<Note> notes, int beatsPerBar)
{
    Analysis a;
    a.beatsPerBar = std::clamp (beatsPerBar, 1, 16);
    a.stepsPerBar = a.beatsPerBar * 4;

    if (notes.empty()) { a.problem = "No notes found in this file."; return a; }

    std::sort (notes.begin(), notes.end(), [] (const Note& x, const Note& y)
               { return x.start != y.start ? x.start < y.start : x.pitch < y.pitch; });

    // Snap to the 16th grid, keep one note per step (the lowest of any chord)
    std::vector<RawStep> raw;
    for (auto& n : notes)
    {
        const int st = (int) std::lround (n.start * 4.0);
        const int len = std::max (1, (int) std::lround (n.length * 4.0));
        if (! raw.empty() && raw.back().step == st)
        {
            a.hadChords = true;
            continue;   // sorted by pitch, so the first one kept is the lowest
        }
        raw.push_back ({ st, len, n.pitch, n.velocity, n.length });
    }
    // Cut notes that run into the next one (rhythm is measured note to note)
    for (size_t i = 0; i + 1 < raw.size(); ++i)
        raw[i].len = std::max (1, std::min (raw[i].len, raw[i + 1].step - raw[i].step));

    // Shift so the clip starts on bar 1 if the file had empty bars at the start
    const int firstBar = raw.front().step / a.stepsPerBar;
    if (firstBar > 0)
        for (auto& r : raw) r.step -= firstBar * a.stepsPerBar;

    int maxEnd = 0;
    for (auto& r : raw) maxEnd = std::max (maxEnd, r.step + r.len);
    a.bars = std::max (1, (maxEnd + a.stepsPerBar - 1) / a.stepsPerBar);
    a.lengthBeats = a.bars * a.beatsPerBar;

    // Velocity, gaps, time per note
    std::vector<double> gaps;
    std::map<int, double> timePerPitch;
    std::map<int, int> countPerPitch;
    long velSum = 0;
    for (auto& r : raw)
    {
        const double nominal = r.len * 0.25;
        const double played = std::min (r.origLength, nominal);
        gaps.push_back (nominal - played);
        timePerPitch[r.pitch] += played;
        countPerPitch[r.pitch]++;
        velSum += r.velocity;
        a.notes.push_back ({ r.step * 0.25, played, r.pitch, r.velocity });
    }
    std::sort (gaps.begin(), gaps.end());
    const double medianGap = gaps[gaps.size() / 2];
    a.gapBeats = medianGap <= 0.005 ? 0.02 : std::clamp (medianGap, 0.01, 0.15);
    a.velocity = (int) (velSum / (long) raw.size());

    a.home = std::max_element (timePerPitch.begin(), timePerPitch.end(),
                               [] (auto& x, auto& y) { return x.second < y.second; })->first;
    a.lowest = countPerPitch.begin()->first;
    a.highest = countPerPitch.rbegin()->first;

    for (auto& kv : countPerPitch)
        a.shares.push_back ({ kv.first, kv.second, (double) kv.second / raw.size() });
    std::sort (a.shares.begin(), a.shares.end(), [&] (const PitchShare& x, const PitchShare& y)
               { return x.count != y.count ? x.count > y.count : timePerPitch[x.pitch] > timePerPitch[y.pitch]; });

    for (size_t i = 0; i + 1 < raw.size(); ++i)
        a.transitions[raw[i].pitch][raw[i + 1].pitch]++;

    // Find the repeating phrase: the shortest block of bars whose rhythm AND notes repeat
    auto onsetsInBlock = [&] (int startBar, int bars)
    {
        std::set<int> s;
        const int lo = startBar * a.stepsPerBar, hi = (startBar + bars) * a.stepsPerBar;
        for (auto& r : raw) if (r.step >= lo && r.step < hi) s.insert (r.step - lo);
        return s;
    };
    auto pitchesInBlock = [&] (int startBar, int bars)
    {
        std::multiset<int> s;
        const int lo = startBar * a.stepsPerBar, hi = (startBar + bars) * a.stepsPerBar;
        for (auto& r : raw) if (r.step >= lo && r.step < hi) s.insert (r.pitch);
        return s;
    };
    a.periodBars = a.bars;
    for (int p = 1; p < a.bars; ++p)
    {
        if (a.bars % p != 0) continue;
        const auto first = onsetsInBlock (0, p);
        const auto firstPitches = pitchesInBlock (0, p);
        if (first.empty()) continue;
        bool repeats = true;
        for (int k = p; k < a.bars && repeats; k += p)
        {
            const auto other = onsetsInBlock (k, p);
            std::vector<int> both, either;
            std::set_intersection (first.begin(), first.end(), other.begin(), other.end(), std::back_inserter (both));
            std::set_union (first.begin(), first.end(), other.begin(), other.end(), std::back_inserter (either));
            const bool rhythmRepeats = ! either.empty() && (double) both.size() / either.size() >= 0.75;

            const auto otherPitches = pitchesInBlock (k, p);
            std::vector<int> common;
            std::set_intersection (firstPitches.begin(), firstPitches.end(), otherPitches.begin(), otherPitches.end(),
                                   std::back_inserter (common));
            const bool notesRepeat = (double) common.size() / std::max (firstPitches.size(), otherPitches.size()) >= 0.6;
            repeats = rhythmRepeats && notesRepeat;
        }
        if (repeats) { a.periodBars = p; break; }
    }

    const int phraseSteps = a.periodBars * a.stepsPerBar;
    for (auto& r : raw)
        if (r.step < phraseSteps)
            a.phrase.push_back ({ r.step, std::min (r.len, phraseSteps - r.step), r.pitch });

    // Guess what kind of part this is
    double meanPitch = 0.0;
    int filled = 0;
    bool allShort = true;
    for (auto& r : raw) { meanPitch += r.pitch; filled += r.len; allShort = allShort && r.len <= 2; }
    meanPitch /= raw.size();
    const double fill = (double) filled / (a.bars * a.stepsPerBar);
    if (meanPitch < 52.0)                a.guessedType = MidiType::bass;     // below E2 (Ableton E1-ish)
    else if (allShort && fill >= 0.6)    a.guessedType = MidiType::arp;
    else                                 a.guessedType = MidiType::melody;

    a.valid = true;
    return a;
}

void generate (const Analysis& a, const Settings& s, LaneSet& lanes, std::uint32_t seed)
{
    if (! a.valid) return;
    Rng rng (seed);

    const int phraseSteps = a.periodBars * a.stepsPerBar;
    const auto srcTokens = tokensFromPhrase (a.phrase, phraseSteps);
    const int notesInPhrase = countNotes (srcTokens);
    const auto bag = buildBag (a, notesInPhrase);

    const double baseLimit = s.rhythm == RhythmMode::locked ? 0.5 : 0.35;
    const LaneSet previous = lanes;   // the batch before this Generate: don't hand those back again

    for (int li = 0; li < numLanes; ++li)
    {
        auto& lane = lanes[(size_t) li];
        if (lane.locked && ! lane.empty) continue;

        std::vector<Note> best;
        double bestScore = 1e9;

        for (int attempt = 0; attempt < 400; ++attempt)
        {
            const auto tokens = makeRhythm (srcTokens, s, rng);
            const auto steps = stepsFromTokens (tokens);
            const double chaos = std::clamp ((attempt - 40) / 160.0, 0.0, 1.0);
            const auto pitches = assignPitches (bag, steps, a, s, rng, chaos);
            auto notes = buildClip (a, s, steps, pitches, rng);

            const double ovSrc = overlap (a.notes, notes);
            double ovLanes = 0.0;
            for (int other = 0; other < numLanes; ++other)
            {
                if (other == li) continue;
                const auto& o = lanes[(size_t) other];
                // compare with lanes already made this round, and with locked lanes
                if (o.empty || (other > li && ! o.locked)) continue;
                ovLanes = std::max (ovLanes, overlap (o.notes, notes));
            }

            bool repeatsOld = false;
            for (auto& old : previous)
                if (! old.empty && ! old.locked && overlap (old.notes, notes) > 0.999) repeatsOld = true;

            const double limit = baseLimit + 0.05 * (attempt / 50);
            const double score = ovSrc + std::max (0.0, ovLanes - 0.7) + (repeatsOld ? 2.0 : 0.0)
                               + (ovLanes > 0.999 ? 2.0 : 0.0);
            if (repeatsOld || ovLanes > 0.999) { if (score < bestScore) { bestScore = score; best = notes; lane.overlapWithSource = ovSrc; } continue; }
            if (score < bestScore) { bestScore = score; best = notes; lane.overlapWithSource = ovSrc; }
            if (ovSrc <= limit && ovLanes <= 0.7 + 0.05 * (attempt / 50)) break;
        }

        lane.notes = std::move (best);
        lane.empty = lane.notes.empty();
    }
}
} // namespace mc
