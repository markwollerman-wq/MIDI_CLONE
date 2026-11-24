#include "Engine.h"
#include "Keys.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <random>
#include <set>

namespace mc
{
namespace
{
using Rng = std::mt19937;
using StepList = std::vector<std::pair<int, int>>;   // (step, length) in 16ths

double uniform (Rng& r)                 { return std::uniform_real_distribution<double> (0.0, 1.0) (r); }
int randInt (Rng& r, int lo, int hi)    { return std::uniform_int_distribution<int> (lo, hi) (r); }   // inclusive
int sign (int x)                        { return (x > 0) - (x < 0); }

struct RawStep { int step, len, pitch, velocity; double origLength; };

// Rhythm as a token list: a value > 0 is a note that long (in 16ths), 0 is a one-16th rest
std::vector<int> tokensFromSteps (const std::vector<Step>& steps, int fromStep, int numSteps)
{
    std::vector<int> t;
    int pos = 0;
    for (auto& s : steps)
    {
        const int rel = s.step - fromStep;
        if (rel < 0 || rel >= numSteps) continue;
        while (pos < rel) { t.push_back (0); ++pos; }
        const int len = std::min (s.length, numSteps - rel);
        t.push_back (len);
        pos += len;
    }
    while (pos < numSteps) { t.push_back (0); ++pos; }
    return t;
}

StepList stepsFromTokens (const std::vector<int>& tokens, int offset = 0)
{
    StepList out;
    int pos = offset;
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
    if (s.rhythm == RhythmMode::locked || src.size() < 3 || countNotes (src) == 0)
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


// Splits a whole-clip count of each item across the phrase repeats, so the clip keeps
// the exact percentages even when the phrase is too short to hold them (extra items go
// to the later repeats, like a turnaround).
std::vector<std::map<int, int>> countsPerRepeat (const std::vector<std::pair<int, double>>& shares, int perPhrase, int repeats)
{
    const int total = perPhrase * repeats;
    std::map<int, int> full;
    {
        std::vector<std::pair<double, int>> rem;
        int used = 0;
        for (auto& sh : shares)
        {
            const double q = sh.second * total;
            full[sh.first] = (int) std::floor (q);
            used += full[sh.first];
            rem.push_back ({ q - std::floor (q), sh.first });
        }
        std::stable_sort (rem.begin(), rem.end(), [] (auto& x, auto& y) { return x.first > y.first; });
        for (size_t k = 0; used < total && k < rem.size(); ++k, ++used) full[rem[k].second]++;
    }
    std::vector<std::map<int, int>> per ((size_t) repeats);
    std::vector<int> need ((size_t) repeats, perPhrase);
    std::vector<int> extras;
    for (auto& kv : full)
    {
        const int base = kv.second / repeats;
        for (int r = 0; r < repeats; ++r) { per[(size_t) r][kv.first] = base; need[(size_t) r] -= base; }
        for (int k = 0; k < kv.second % repeats; ++k) extras.push_back (kv.first);
    }
    // hand out the extras starting with the last repeat
    int r = repeats - 1;
    for (int item : extras)
    {
        for (int tries = 0; tries < repeats && need[(size_t) r] <= 0; ++tries) r = (r + repeats - 1) % repeats;
        per[(size_t) r][item]++;
        need[(size_t) r]--;
        r = (r + repeats - 1) % repeats;
    }
    return per;
}

// Match by playing time: swaps notes so each pitch gets as close as possible to its time goal
void rebalanceTime (std::vector<int>& seq, const StepList& steps, const std::map<int, double>& goal)
{
    auto error = [&] (const std::vector<int>& q)
    {
        std::map<int, double> t;
        for (size_t i = 0; i < q.size(); ++i) t[q[i]] += steps[i].second;
        double e = 0.0;
        for (auto& g : goal) e += std::abs (t[g.first] - g.second);
        return e;
    };
    double cur = error (seq);
    for (int iter = 0; iter < (int) seq.size() * 4; ++iter)
    {
        double bestGain = 1e-9;
        int bestI = -1, bestP = 0;
        for (int i = 1; i < (int) seq.size(); ++i)   // keep the first note
            for (auto& g : goal)
            {
                if (g.first == seq[(size_t) i]) continue;
                auto q = seq;
                q[(size_t) i] = g.first;
                const double gain = cur - error (q);
                if (gain > bestGain) { bestGain = gain; bestI = i; bestP = g.first; }
            }
        if (bestI < 0) break;
        seq[(size_t) bestI] = bestP;
        cur -= bestGain;
    }
}

std::vector<int> bagFromCounts (const std::map<int, int>& counts)
{
    std::vector<int> bag;
    for (auto& kv : counts) for (int k = 0; k < kv.second; ++k) bag.push_back (kv.first);
    return bag;
}

// Changes as few items as possible in seq so it has exactly the wanted counts
// (keeps the first and last item if it can)
void rebalance (std::vector<int>& seq, const std::map<int, int>& wanted, Rng& rng)
{
    std::map<int, int> have;
    for (int x : seq) have[x]++;
    std::vector<int> positions ((size_t) seq.size());
    std::iota (positions.begin(), positions.end(), 0);
    std::shuffle (positions.begin(), positions.end(), rng);
    std::stable_partition (positions.begin(), positions.end(), [&] (int i) { return i != 0 && i != (int) seq.size() - 1; });
    for (int i : positions)
    {
        const int cur = seq[(size_t) i];
        auto wit = wanted.find (cur);
        const int want = wit == wanted.end() ? 0 : wit->second;
        if (have[cur] <= want) continue;
        for (auto& w : wanted)
            if (have[w.first] < w.second)
            {
                have[cur]--; have[w.first]++;
                seq[(size_t) i] = w.first;
                break;
            }
    }
}

// Note counts for a number of notes, keeping the source's percentages (largest remainder rounding)
std::vector<int> buildBag (const Analysis& a, int numNotes)
{
    const int total = std::accumulate (a.shares.begin(), a.shares.end(), 0,
                                       [] (int acc, const PitchShare& p) { return acc + p.count; });
    std::vector<int> counts;
    std::vector<std::pair<double, size_t>> remainders;
    int used = 0;
    for (size_t i = 0; i < a.shares.size(); ++i)
    {
        const double quota = (double) a.shares[i].count * numNotes / std::max (1, total);
        const int c = (int) std::floor (quota);
        counts.push_back (c);
        used += c;
        remainders.push_back ({ quota - c, i });
    }
    std::stable_sort (remainders.begin(), remainders.end(),
                      [] (auto& x, auto& y) { return x.first > y.first; });
    for (size_t k = 0; used < numNotes && k < remainders.size(); ++k, ++used)
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

std::vector<int> contourOf (const std::vector<int>& pitches)
{
    std::vector<int> c;
    for (size_t i = 1; i < pitches.size(); ++i) c.push_back (sign (pitches[i] - pitches[i - 1]));
    return c;
}

double scoreOrder (const std::vector<int>& seq, const StepList& steps, const Analysis& a, const Settings& s,
                   const std::vector<int>& contour, const std::map<int, int>* barStarts)
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

        // Keep Shape: go up where the source goes up, down where it goes down
        if (s.keepShape && i > 0 && (size_t) (i - 1) < contour.size())
            score += (sign (p - prev) == contour[(size_t) i - 1]) ? 0.8 : -0.8;

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

            case MidiType::stab:
                if (barStarts != nullptr)
                {
                    auto it = barStarts->find (st);                            // same note on beat 1 as the source
                    if (it != barStarts->end() && it->second == p) score += 1.5;
                }
                if (jump > 4) score -= 0.8;                                    // stabs stay tight
                if (jump > 7) score -= 1.0;
                break;
        }
    }
    return score;
}

// Finds a good order for the notes in the bag. With allowChanges, a note may also
// be swapped for another source note (used when the score itself checks the percentages).
std::vector<int> optimise (std::vector<int> bag, const std::function<double (const std::vector<int>&)>& scorer,
                           const Analysis& a, const Settings& s, Rng& rng, double chaos, bool allowChanges)
{
    std::shuffle (bag.begin(), bag.end(), rng);
    const int n = (int) bag.size();
    if (n < 2) return bag;
    // Short clips have few possible orders: when the best ones are used up, take a random order.
    // (When the score also checks the percentages, explore more widely instead.)
    const bool searchWide = allowChanges || s.keepShape;
    if (! searchWide && uniform (rng) < chaos) return bag;

    auto cur = bag;
    double curScore = scorer (cur);
    auto best = cur;
    double bestScore = curScore;
    const double temperature = (0.04 + 0.05 * s.variation) * (searchWide ? 1.0 + 4.0 * chaos : 1.0);
    const int iterations = 300 + 60 * n;

    for (int it = 0; it < iterations; ++it)
    {
        auto next = cur;
        if (allowChanges && uniform (rng) < 0.3)
        {
            next[(size_t) randInt (rng, 0, n - 1)] = a.shares[(size_t) randInt (rng, 0, (int) a.shares.size() - 1)].pitch;
        }
        else
        {
            const int i = randInt (rng, 0, n - 1), j = randInt (rng, 0, n - 1);
            if (next[(size_t) i] == next[(size_t) j]) continue;
            std::swap (next[(size_t) i], next[(size_t) j]);
        }
        const double sc = scorer (next);
        if (sc >= curScore || uniform (rng) < std::exp ((sc - curScore) / temperature))
        {
            cur = std::move (next);
            curScore = sc;
            if (sc > bestScore) { bestScore = sc; best = cur; }
        }
    }
    // Higher variation: sometimes keep a less "perfect" but still good order
    return (! allowChanges && uniform (rng) < s.variation / 20.0) ? cur : best;
}

// New Notes: replace some notes with in-key notes the source doesn't use.
// Notes to replace are taken from each source note in proportion to its share.
int addNewNotes (std::vector<int>& pitches, const StepList& steps, const Analysis& a, const Settings& s, Rng& rng)
{
    if (s.newNotes <= 0.0f || s.keyScale < 0 || pitches.size() < 2) return 0;
    const Key key { s.keyRoot, s.keyScale };
    const int n = (int) pitches.size();
    const int wanted = (int) std::lround (s.newNotes * n);
    if (wanted == 0) return 0;

    std::set<int> sourcePitches;
    for (auto& sh : a.shares) sourcePitches.insert (sh.pitch);
    std::vector<int> pool;
    for (int p = a.lowest - 2; p <= a.highest + 2; ++p)
        if (inKey (p, key) && ! sourcePitches.count (p)) pool.push_back (p);
    if (pool.empty()) return 0;

    // how many of each source note to replace (proportional to its share)
    std::map<int, int> quota;
    {
        std::vector<std::pair<double, int>> rem;
        int used = 0;
        for (auto& sh : a.shares)
        {
            const double q = sh.share * wanted;
            quota[sh.pitch] = (int) std::floor (q);
            used += quota[sh.pitch];
            rem.push_back ({ q - std::floor (q), sh.pitch });
        }
        std::sort (rem.begin(), rem.end(), [] (auto& x, auto& y) { return x.first > y.first; });
        for (size_t k = 0; used < wanted && k < rem.size(); ++k, ++used) quota[rem[k].second]++;
    }

    std::vector<int> order ((size_t) n);
    std::iota (order.begin(), order.end(), 0);
    std::shuffle (order.begin(), order.end(), rng);
    const bool tight = s.type == MidiType::stab;

    auto eligible = [&] (int i)
    {
        const int st = steps[(size_t) i].first;
        switch (s.type)
        {
            case MidiType::bass:   return st % 4 != 0;                           // off the beat
            case MidiType::stab:   return st % a.stepsPerBar != 0;              // never beat 1
            case MidiType::melody: return i != n - 1 && st % a.stepsPerBar != 0;
            case MidiType::arp:    return i != 0;
        }
        return true;
    };

    int placed = 0;
    for (int pass = 0; pass < 2 && placed < wanted; ++pass)
    {
        for (int i : order)
        {
            if (placed >= wanted) break;
            const int old = pitches[(size_t) i];
            if (! sourcePitches.count (old) || ! eligible (i)) continue;
            if (pass == 0 && quota[old] <= 0) continue;   // second pass relaxes the proportions

            const int prev = i > 0 ? pitches[(size_t) i - 1] : -1;
            const int next = i + 1 < n ? pitches[(size_t) i + 1] : -1;
            std::vector<std::pair<int, double>> cands;
            for (int c : pool)
            {
                if (tight && std::abs (c - old) > 2) continue;
                if (s.keepShape)
                {
                    if (prev >= 0 && sign (c - prev) != sign (old - prev)) continue;
                    if (next >= 0 && sign (next - c) != sign (next - old)) continue;
                }
                int nearest = 99;
                for (int sp : sourcePitches) nearest = std::min (nearest, std::abs (c - sp));
                cands.push_back ({ c, 1.0 / (1.0 + nearest) + 1.0 / (1.0 + std::abs (c - old)) });
            }
            if (cands.empty()) continue;
            double total = 0.0;
            for (auto& c : cands) total += c.second;
            double r = uniform (rng) * total;
            int pick = cands.back().first;
            for (auto& c : cands) { r -= c.second; if (r <= 0) { pick = c.first; break; } }

            pitches[(size_t) i] = pick;
            quota[old]--;
            ++placed;
        }
    }
    return placed;
}

std::vector<Note> notesFromSteps (const Analysis& a, const Settings& s, const std::vector<Step>& all, Rng& rng)
{
    std::vector<Note> notes;
    for (size_t k = 0; k < all.size(); ++k)
    {
        const auto& st = all[k];
        const double nominal = st.length * 0.25;
        double gap = a.gapBeats;
        if (s.type == MidiType::arp) gap = std::max (gap, nominal * 0.25);
        if (s.type == MidiType::stab) gap = std::max (gap, nominal * 0.1);   // keep stabs detached
        double len = std::max (nominal - gap, nominal * 0.5);

        if (s.slide && k + 1 < all.size())
        {
            const auto& next = all[k + 1];
            if (next.step == st.step + st.length && next.pitch != st.pitch && uniform (rng) < s.slideAmount)
                len = nominal + 0.03125;   // overlap the next note so the synth glides into it
        }

        int vel = a.velocity;
        if (s.type == MidiType::melody || s.type == MidiType::arp)
            vel += (st.step % 4 == 0) ? 8 : (st.step % 2 == 1 ? -6 : 0);

        notes.push_back ({ st.step * 0.25, len, st.pitch, std::clamp (vel, 1, 127) });
    }
    return notes;
}

// ---- one candidate clip for the normal types (phrase + repeats) ----
std::vector<Note> makePhraseClip (const Analysis& a, const Settings& s, Rng& rng, double chaos, int& newCount)
{
    const int phraseSteps = a.periodBars * a.stepsPerBar;
    const int repeats = std::max (1, a.bars / a.periodBars);
    const auto srcTokens = tokensFromSteps (a.phrase, 0, phraseSteps);
    std::vector<std::pair<int, double>> shareList;
    for (auto& sh : a.shares) shareList.push_back ({ sh.pitch, sh.share });
    const auto perRepeat = countsPerRepeat (shareList, countNotes (srcTokens), repeats);
    const auto bag = bagFromCounts (perRepeat.front());

    std::vector<int> srcPitches;
    for (auto& st : a.phrase) srcPitches.push_back (st.pitch);
    const auto contour = contourOf (srcPitches);

    const auto tokens = makeRhythm (srcTokens, s, rng);
    const auto steps = stepsFromTokens (tokens);
    // Match by playing time: each note should get its share of the playing time, not of the note count
    std::map<int, double> timeTarget;
    double stepTotal = 0.0;
    for (auto& st : steps) stepTotal += st.second;
    for (auto& sh : a.shares) timeTarget[sh.pitch] = sh.timeShare * stepTotal;
    const double avgLen = steps.empty() ? 1.0 : stepTotal / steps.size();
    auto scorer = [&] (const std::vector<int>& seq)
    {
        double sc = scoreOrder (seq, steps, a, s, contour, nullptr);
        if (s.matchTime)
        {
            std::map<int, double> t;
            for (size_t i = 0; i < seq.size(); ++i) t[seq[i]] += steps[i].second;
            double err = 0.0;
            for (auto& kv : timeTarget) err += std::abs (t[kv.first] - kv.second);
            sc -= 4.0 * err / avgLen;
        }
        return sc;
    };
    auto pitches = optimise (bag, scorer, a, s, rng, chaos, s.matchTime);
    newCount = addNewNotes (pitches, steps, a, s, rng) * repeats;

    std::vector<Step> all;
    std::map<int, double> timeSoFar;
    for (int r = 0; r < repeats; ++r)
    {
        auto seq = pitches;
        if (newCount == 0)
        {
            if (! s.matchTime) rebalance (seq, perRepeat[(size_t) r], rng);   // exact clip percentages
            else
            {
                // playing time: aim each repeat at what the clip still needs by its end
                std::map<int, double> goal;
                for (auto& sh : a.shares) goal[sh.pitch] = sh.timeShare * stepTotal * (r + 1) - timeSoFar[sh.pitch];
                rebalanceTime (seq, steps, goal);
                for (size_t i = 0; i < seq.size(); ++i) timeSoFar[seq[i]] += steps[i].second;
            }
        }
        // Last time through: swap two different notes (same counts, a little variation)
        // Last time through: swap two different notes (same counts, a little variation)
        if (repeats > 1 && r == repeats - 1 && s.type != MidiType::arp
            && uniform (rng) < s.variation / 10.0 && seq.size() > 1)
        {
            for (int tries = 0; tries < 20; ++tries)
            {
                const int i = randInt (rng, 0, (int) seq.size() - 1), j = randInt (rng, 0, (int) seq.size() - 1);
                if (s.matchTime && steps[(size_t) i].second != steps[(size_t) j].second) continue;
                if (seq[(size_t) i] != seq[(size_t) j]) { std::swap (seq[(size_t) i], seq[(size_t) j]); break; }
            }
        }
        for (size_t k = 0; k < steps.size(); ++k)
            all.push_back ({ r * phraseSteps + steps[k].first, steps[k].second, seq[k] });
    }
    return notesFromSteps (a, s, all, rng);
}

// ---- one candidate clip for Stab / Riff: bars that repeat in the source repeat in the clone ----
std::vector<Note> makeStabClip (const Analysis& a, const Settings& s, Rng& rng, double chaos, int& newCount)
{
    // One "template" bar per group of identical source bars
    std::vector<int> templateOfBar ((size_t) a.bars, 0);
    std::vector<int> templateBars;
    std::map<int, int> groupToTemplate;
    for (int b = 0; b < a.bars; ++b)
    {
        const int g = b < (int) a.barGroup.size() ? a.barGroup[(size_t) b] : b;
        auto it = groupToTemplate.find (g);
        if (it == groupToTemplate.end())
        {
            groupToTemplate[g] = (int) templateBars.size();
            templateBars.push_back (b);
        }
        templateOfBar[(size_t) b] = groupToTemplate[g];
    }

    // Rhythm of each template bar (Locked keeps it, Same Feel reshuffles inside the bar)
    std::vector<StepList> barSteps;   // per template, steps relative to the bar
    for (int b : templateBars)
    {
        const auto src = tokensFromSteps (a.allSteps, b * a.stepsPerBar, a.stepsPerBar);
        barSteps.push_back (stepsFromTokens (makeRhythm (src, s, rng)));
    }

    // Template note list, and how it expands to the whole clip
    StepList templateSteps;
    std::vector<int> templateStart;   // first template note index of each template bar
    for (size_t t = 0; t < barSteps.size(); ++t)
    {
        templateStart.push_back ((int) templateSteps.size());
        for (auto& st : barSteps[t]) templateSteps.push_back ({ st.first + (int) t * a.stepsPerBar, st.second });
    }
    StepList expandedSteps;
    std::vector<int> expandIndex;
    for (int b = 0; b < a.bars; ++b)
    {
        const int t = templateOfBar[(size_t) b];
        for (size_t k = 0; k < barSteps[(size_t) t].size(); ++k)
        {
            expandedSteps.push_back ({ b * a.stepsPerBar + barSteps[(size_t) t][k].first, barSteps[(size_t) t][k].second });
            expandIndex.push_back (templateStart[(size_t) t] + (int) k);
        }
    }
    if (templateSteps.empty()) return {};

    std::vector<int> srcPitches;
    for (auto& st : a.allSteps) srcPitches.push_back (st.pitch);
    const auto contour = contourOf (srcPitches);
    std::map<int, int> barStarts;
    for (auto& st : a.allSteps) if (st.step % a.stepsPerBar == 0) barStarts[st.step] = st.pitch;

    // targets: note counts, or playing time (in 16ths) with Match by playing time
    std::map<int, double> target;
    double expandedTime = 0.0;
    for (auto& st : expandedSteps) expandedTime += st.second;
    for (auto& sh : a.shares)
        target[sh.pitch] = s.matchTime ? sh.timeShare * expandedTime : sh.share * (double) expandIndex.size();
    const double unit = s.matchTime ? expandedTime / std::max<size_t> (1, expandIndex.size()) : 1.0;

    auto expand = [&] (const std::vector<int>& seq)
    {
        std::vector<int> e;
        e.reserve (expandIndex.size());
        for (int idx : expandIndex) e.push_back (seq[(size_t) idx]);
        return e;
    };
    auto scorer = [&] (const std::vector<int>& seq)
    {
        const auto e = expand (seq);
        std::map<int, double> amount;
        for (size_t k = 0; k < e.size(); ++k) amount[e[k]] += s.matchTime ? expandedSteps[k].second : 1.0;
        double err = 0.0;
        for (auto& t : target) err += std::abs (amount[t.first] - t.second);
        return scoreOrder (e, expandedSteps, a, s, contour, &barStarts) - 4.0 * err / unit;
    };

    auto pitches = optimise (buildBag (a, (int) templateSteps.size()), scorer, a, s, rng, chaos, true);

    // Keep the percentages: reject a clip that's more than one note off for any source note
    {
        std::map<int, double> amount;
        for (size_t k = 0; k < expandIndex.size(); ++k)
            amount[pitches[(size_t) expandIndex[k]]] += s.matchTime ? expandedSteps[k].second : 1.0;
        for (auto& t : target)
            if (std::abs (amount[t.first] - t.second) > unit * (s.matchTime ? 2.0 : 1.0) + 1e-9 && chaos < 0.99) return {};
    }
    addNewNotes (pitches, templateSteps, a, s, rng);

    std::set<int> sourcePitches;
    for (auto& sh : a.shares) sourcePitches.insert (sh.pitch);
    std::vector<Step> all;
    newCount = 0;
    for (size_t k = 0; k < expandIndex.size(); ++k)
    {
        const int p = pitches[(size_t) expandIndex[k]];
        newCount += sourcePitches.count (p) ? 0 : 1;
        all.push_back ({ expandedSteps[k].first, expandedSteps[k].second, p });
    }
    return notesFromSteps (a, s, all, rng);
}

// ================= Chords =================
struct ChordTemplate { int rel; const char* suffix; };

int maskOf (std::initializer_list<int> ivs) { int m = 0; for (int iv : ivs) m |= 1 << (iv % 12); return m; }
int rotateMask (int mask, int by) { by = ((by % 12) + 12) % 12; return ((mask >> by) | (mask << (12 - by))) & 0xfff; }
int popcount (int m) { int c = 0; while (m) { c += m & 1; m >>= 1; } return c; }

const std::vector<ChordTemplate>& chordTemplates()
{
    static const std::vector<ChordTemplate> t = {
        { maskOf ({ 0, 4, 7 }), "" },        { maskOf ({ 0, 3, 7 }), "m" },
        { maskOf ({ 0, 3, 6 }), "dim" },     { maskOf ({ 0, 4, 8 }), "aug" },
        { maskOf ({ 0, 2, 7 }), "sus2" },    { maskOf ({ 0, 5, 7 }), "sus4" },
        { maskOf ({ 0, 4, 7, 10 }), "7" },   { maskOf ({ 0, 4, 7, 11 }), "maj7" },
        { maskOf ({ 0, 3, 7, 10 }), "m7" },  { maskOf ({ 0, 3, 6, 10 }), "m7b5" },
        { maskOf ({ 0, 4, 7, 9 }), "6" },    { maskOf ({ 0, 3, 7, 9 }), "m6" },
        { maskOf ({ 0, 2, 4, 7 }), "add9" }, { maskOf ({ 0, 2, 3, 7 }), "madd9" },
        { maskOf ({ 0, 7 }), "5" },
    };
    return t;
}

// Root of a chord: the bass note if it forms a known chord, otherwise any note that does
int chordRoot (int mask, int bassPc, int* templateIndex = nullptr)
{
    std::vector<int> roots;
    if (bassPc >= 0 && (mask >> bassPc & 1)) roots.push_back (bassPc);
    for (int r = 0; r < 12; ++r) if ((mask >> r & 1) && r != bassPc) roots.push_back (r);
    for (int r : roots)
    {
        const int rel = rotateMask (mask, r);
        const auto& t = chordTemplates();
        for (size_t i = 0; i < t.size(); ++i)
            if (t[i].rel == rel) { if (templateIndex) *templateIndex = (int) i; return r; }
    }
    if (templateIndex) *templateIndex = -1;
    return roots.empty() ? 0 : roots.front();
}

double chordTransitionProb (const Analysis& a, int from, int to)
{
    auto it = a.chordTransitions.find (from);
    if (it == a.chordTransitions.end()) return 0.0;
    int total = 0;
    for (auto& kv : it->second) total += kv.second;
    auto jt = it->second.find (to);
    return (jt == it->second.end() || total == 0) ? 0.0 : (double) jt->second / total;
}

// Picks the inversion / octave of a chord that moves least from the previous chord
std::vector<int> voiceChord (const std::vector<int>& base, const std::vector<int>& prev, int lo, int hi,
                             int topDirection, Rng& rng, int variation)
{
    std::vector<std::vector<int>> cands;
    const int n = (int) base.size();
    for (int inv = 0; inv < n; ++inv)
    {
        auto v = base;
        for (int j = 0; j < inv; ++j) v[(size_t) j] += 12;
        std::sort (v.begin(), v.end());
        for (int oct : { -12, 0, 12 })
        {
            auto w = v;
            bool ok = true;
            for (auto& p : w) { p += oct; ok = ok && p >= lo - 3 && p <= hi + 3 && p >= 0 && p <= 127; }
            if (ok) cands.push_back (w);
        }
    }
    if (cands.empty()) return base;
    if (prev.empty())
        return std::find (cands.begin(), cands.end(), base) != cands.end() ? base : cands.front();

    double bestCost = 1e18;
    std::vector<int> best = cands.front();
    for (auto& w : cands)
    {
        double move = 0.0;
        for (int p : w)    { int d = 99; for (int q : prev) d = std::min (d, std::abs (p - q)); move += d; }
        for (int q : prev) { int d = 99; for (int p : w)    d = std::min (d, std::abs (p - q)); move += d; }
        double cost = move / 2.0;
        if (topDirection != 0 && sign (w.back() - prev.back()) != topDirection) cost += 6.0;   // Keep Shape (top note)
        if (w != base) cost += 0.5;
        cost += uniform (rng) * variation * 0.3;
        if (cost < bestCost) { bestCost = cost; best = w; }
    }
    return best;
}

// New Chords: swap some chords for in-key triads the source doesn't use (sharing notes with the old chord)
int addNewChords (std::vector<int>& order, const Analysis& a, const Settings& s, Rng& rng,
                  std::map<int, std::vector<int>>& newVoicings)
{
    if (s.newNotes <= 0.0f || s.keyScale < 0) return 0;
    const auto& iv = scales()[(size_t) s.keyScale].intervals;
    const int n = (int) order.size();
    if (iv.size() != 7 || n < 3) return 0;
    const int wanted = (int) std::lround (s.newNotes * n);
    if (wanted == 0) return 0;

    std::set<int> used;
    for (auto& c : a.chordShares) used.insert (c.mask);
    struct Triad { int mask; std::array<int, 3> pcs; };
    std::vector<Triad> triads;
    for (int d = 0; d < 7; ++d)
    {
        std::array<int, 3> pcs { (s.keyRoot + iv[(size_t) d]) % 12, (s.keyRoot + iv[(size_t) ((d + 2) % 7)]) % 12,
                                 (s.keyRoot + iv[(size_t) ((d + 4) % 7)]) % 12 };
        const int m = (1 << pcs[0]) | (1 << pcs[1]) | (1 << pcs[2]);
        if (! used.count (m)) triads.push_back ({ m, pcs });
    }
    if (triads.empty()) return 0;

    std::vector<int> positions;
    for (int i = 1; i < n - 1; ++i) positions.push_back (i);
    std::shuffle (positions.begin(), positions.end(), rng);

    int placed = 0;
    for (int pos : positions)
    {
        if (placed >= wanted) break;
        const int old = order[(size_t) pos];
        if (! used.count (old)) continue;
        std::vector<std::pair<const Triad*, double>> cands;
        for (auto& t : triads)
            if (popcount (t.mask & old) >= 1) cands.push_back ({ &t, popcount (t.mask & old) + 0.5 });
        if (cands.empty()) continue;
        double total = 0.0;
        for (auto& c : cands) total += c.second;
        double r = uniform (rng) * total;
        const Triad* pick = cands.back().first;
        for (auto& c : cands) { r -= c.second; if (r <= 0) { pick = c.first; break; } }

        if (! newVoicings.count (pick->mask))
        {
            // root-position triad near the old chord, with as many notes as the old voicing
            auto it = a.voicing.find (old);
            const auto oldV = it != a.voicing.end() ? it->second : std::vector<int> { 60 };
            int root = oldV.front() - 6;
            while (((root % 12) + 12) % 12 != pick->pcs[0]) ++root;
            std::vector<int> v { root };
            for (int k = 1; k < 3; ++k)
            {
                int p = v.back() + 1;
                while (((p % 12) + 12) % 12 != pick->pcs[(size_t) k]) ++p;
                v.push_back (p);
            }
            for (size_t k = 3; k < oldV.size(); ++k) v.push_back (v[(k - 3) % 3] + 12);   // double notes for bigger voicings
            if (oldV.size() == 2) v.pop_back();
            std::sort (v.begin(), v.end());
            newVoicings[pick->mask] = v;
        }
        order[(size_t) pos] = pick->mask;
        ++placed;
    }
    return placed;
}

std::vector<Note> makeChordClip (const Analysis& a, const Settings& s, Rng& rng, double chaos, int& newCount)
{
    if (a.chordPhrase.empty()) return {};
    const int phraseSteps = a.chordPeriodBars * a.stepsPerBar;
    const int repeats = std::max (1, a.bars / a.chordPeriodBars);

    std::vector<Step> src;
    for (auto& c : a.chordPhrase) src.push_back ({ c.step, c.length, 0 });
    const auto tokens = makeRhythm (tokensFromSteps (src, 0, phraseSteps), s, rng);
    const auto steps = stepsFromTokens (tokens);
    const int n = (int) steps.size();
    if (n == 0) return {};

    // chord counts for each repeat, keeping the whole clip's chord percentages
    std::vector<std::pair<int, double>> shareList;
    for (auto& c : a.chordShares) shareList.push_back ({ c.mask, c.share });
    const auto perRepeat = countsPerRepeat (shareList, n, repeats);
    const auto bag = bagFromCounts (perRepeat.front());

    std::vector<int> srcTop;
    for (auto& c : a.chordPhrase) srcTop.push_back (c.pitches.back());
    const auto contour = contourOf (srcTop);
    std::map<int, int> barStarts;
    for (auto& c : a.chordPhrase) if (c.step % a.stepsPerBar == 0) barStarts[c.step] = c.mask;

    auto scorer = [&] (const std::vector<int>& seq)
    {
        const double mw = 1.5 * (11 - s.variation) / 10.0;
        double score = 0.0;
        for (int i = 0; i < (int) seq.size(); ++i)
        {
            const int m = seq[(size_t) i];
            if (i > 0)
            {
                const int prev = seq[(size_t) i - 1];
                const double tp = chordTransitionProb (a, prev, m);
                score += mw * tp;
                if (m == prev && tp <= 0.0) score -= 0.6;
            }
            if (i == 0 && m == a.homeChord) score += 2.0;                     // start on the home chord
            if (i == (int) seq.size() - 1 && m == a.homeChord) score += 1.5;  // and come back to it
            auto it = barStarts.find (steps[(size_t) i].first);
            if (it != barStarts.end() && it->second == m) score += 0.8;
        }
        return score;
    };
    auto order = optimise (bag, scorer, a, s, rng, chaos, false);

    std::map<int, std::vector<int>> newVoicings;
    const int newInPhrase = addNewChords (order, a, s, rng, newVoicings);
    newCount = newInPhrase * repeats;

    int lo = 127, hi = 0;
    for (auto& nn : a.polyNotes) { lo = std::min (lo, nn.pitch); hi = std::max (hi, nn.pitch); }

    std::vector<Note> notes;
    std::vector<int> prevVoicing;
    for (int r = 0; r < repeats; ++r)
    {
        auto seq = order;
        if (newInPhrase == 0) rebalance (seq, perRepeat[(size_t) r], rng);   // exact clip percentages
        if (repeats > 1 && r == repeats - 1 && uniform (rng) < s.variation / 10.0 && seq.size() > 2)
        {
            for (int tries = 0; tries < 20; ++tries)
            {
                const int i = randInt (rng, 1, (int) seq.size() - 1), j = randInt (rng, 1, (int) seq.size() - 1);
                if (seq[(size_t) i] != seq[(size_t) j]) { std::swap (seq[(size_t) i], seq[(size_t) j]); break; }
            }
        }
        for (int k = 0; k < n; ++k)
        {
            const int m = seq[(size_t) k];
            auto vit = a.voicing.find (m);
            const auto base = vit != a.voicing.end() ? vit->second : newVoicings[m];
            const int dir = (s.keepShape && k > 0 && (size_t) (k - 1) < contour.size()) ? contour[(size_t) k - 1] : 0;
            const auto v = voiceChord (base, prevVoicing, lo, hi, dir, rng, s.variation);
            prevVoicing = v;

            const double nominal = steps[(size_t) k].second * 0.25;
            const double len = std::max (nominal - a.chordGap, nominal * 0.5);
            const double start = (r * phraseSteps + steps[(size_t) k].first) * 0.25;
            for (int p : v) notes.push_back ({ start, len, p, a.velocity });
        }
    }
    return notes;
}

} // namespace

std::string noteName (int pitch)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[((pitch % 12) + 12) % 12];
}

std::string chordName (int mask, int bassPc)
{
    if (mask == 0) return "-";
    int t = -1;
    const int root = chordRoot (mask, bassPc, &t);
    if (popcount (mask) == 1) return noteName (root) + " (note)";
    if (t < 0) return noteName (root) + " (cluster)";
    return noteName (root) + chordTemplates()[(size_t) t].suffix;
}

std::string noteNameWithOctave (int pitch)
{
    return noteName (pitch) + std::to_string (pitch / 12 - 2);
}

RhythmMode defaultRhythmFor (MidiType t)
{
    return t == MidiType::melody ? RhythmMode::sameFeel : RhythmMode::locked;
}

bool defaultKeepShapeFor (MidiType t)
{
    return t == MidiType::melody || t == MidiType::stab || t == MidiType::chords;
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

Analysis analyse (std::vector<Note> notes, int beatsPerBar, int homePitchClass)
{
    Analysis a;
    a.beatsPerBar = std::clamp (beatsPerBar, 1, 16);
    a.stepsPerBar = a.beatsPerBar * 4;

    if (notes.empty()) { a.problem = "No notes found in this file."; return a; }

    std::sort (notes.begin(), notes.end(), [] (const Note& x, const Note& y)
               { return x.start < y.start - 1e-9 || (std::abs (x.start - y.start) <= 1e-9 && x.pitch < y.pitch); });

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
    const int firstBar = std::max (0, raw.front().step / a.stepsPerBar);
    a.offsetBeats = firstBar * a.beatsPerBar;
    if (firstBar > 0)
        for (auto& r : raw) r.step -= firstBar * a.stepsPerBar;

    int maxEnd = 0;
    for (auto& r : raw) maxEnd = std::max (maxEnd, r.step + r.len);
    a.bars = std::max (1, (maxEnd + a.stepsPerBar - 1) / a.stepsPerBar);
    a.lengthBeats = a.bars * a.beatsPerBar;

    // Velocity, gaps, counts
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
        a.allSteps.push_back ({ r.step, r.len, r.pitch });
    }
    std::sort (gaps.begin(), gaps.end());
    const double medianGap = gaps[gaps.size() / 2];
    a.gapBeats = medianGap <= 0.005 ? 0.02 : std::clamp (medianGap, 0.01, 0.15);
    a.velocity = (int) (velSum / (long) raw.size());
    a.lowest = countPerPitch.begin()->first;
    a.highest = countPerPitch.rbegin()->first;

    // Home note: the key's root if given, otherwise the note the clip keeps landing on
    int homePc = homePitchClass;
    bool hintPresent = false;
    for (auto& kv : countPerPitch) hintPresent = hintPresent || (kv.first % 12 == homePc);
    if (homePc < 0 || ! hintPresent)
    {
        const auto w = homeWeights (a.notes, a.beatsPerBar);
        homePc = std::max_element (w.begin(), w.end(), [] (auto& x, auto& y) { return x.second < y.second; })->first;
    }
    double bestTime = -1.0;
    for (auto& kv : timePerPitch)
        if (kv.first % 12 == homePc && kv.second > bestTime) { bestTime = kv.second; a.home = kv.first; }

    double totalTime = 0.0;
    for (auto& kv : timePerPitch) totalTime += kv.second;
    for (auto& kv : countPerPitch)
        a.shares.push_back ({ kv.first, kv.second, (double) kv.second / raw.size(),
                              totalTime > 0 ? timePerPitch[kv.first] / totalTime : 0.0 });
    std::sort (a.shares.begin(), a.shares.end(), [&] (const PitchShare& x, const PitchShare& y)
               { return x.count != y.count ? x.count > y.count : timePerPitch[x.pitch] > timePerPitch[y.pitch]; });

    for (size_t i = 0; i + 1 < raw.size(); ++i)
        a.transitions[raw[i].pitch][raw[i + 1].pitch]++;


    // ---- Chords: group every note by its start step ----
    {
        std::map<int, std::vector<const Note*>> byStep;
        for (auto& n : notes)
        {
            const int st = (int) std::lround (n.start * 4.0) - firstBar * a.stepsPerBar;
            byStep[st].push_back (&n);
            a.polyNotes.push_back ({ n.start - a.offsetBeats, n.length, n.pitch, n.velocity });
        }
        std::vector<double> chordGaps;
        for (auto& kv : byStep)
        {
            ChordEvent e;
            e.step = kv.first;
            std::set<int> ps;
            double maxLen = 0.0;
            long vs = 0;
            for (auto* n : kv.second) { ps.insert (n->pitch); maxLen = std::max (maxLen, n->length); vs += n->velocity; }
            e.pitches.assign (ps.begin(), ps.end());
            e.length = std::max (1, (int) std::lround (maxLen * 4.0));
            e.velocity = (int) (vs / (long) kv.second.size());
            for (int p : e.pitches) e.mask |= 1 << (p % 12);
            chordGaps.push_back (maxLen);   // replaced with the gap below
            a.chords.push_back (e);
        }
        for (size_t i = 0; i < a.chords.size(); ++i)
        {
            auto& c = a.chords[i];
            if (i + 1 < a.chords.size()) c.length = std::max (1, std::min (c.length, a.chords[i + 1].step - c.step));
            chordGaps[i] = c.length * 0.25 - std::min (chordGaps[i], c.length * 0.25);
        }
        std::sort (chordGaps.begin(), chordGaps.end());
        const double mg = chordGaps.empty() ? 0.0 : chordGaps[chordGaps.size() / 2];
        a.chordGap = mg <= 0.005 ? 0.02 : std::clamp (mg, 0.01, 0.25);

        std::map<int, int> counts;
        int multi = 0;
        for (size_t i = 0; i < a.chords.size(); ++i)
        {
            const auto& c = a.chords[i];
            counts[c.mask]++;
            multi += c.pitches.size() >= 2 ? 1 : 0;
            if (! a.voicing.count (c.mask)) a.voicing[c.mask] = c.pitches;
            if (i + 1 < a.chords.size()) a.chordTransitions[c.mask][a.chords[i + 1].mask]++;
        }
        a.chordRatio = a.chords.empty() ? 0.0 : (double) multi / a.chords.size();
        for (auto& kv : counts)
            a.chordShares.push_back ({ kv.first, kv.second, (double) kv.second / a.chords.size(),
                                       chordName (kv.first, a.voicing[kv.first].front() % 12) });
        std::stable_sort (a.chordShares.begin(), a.chordShares.end(), [] (auto& x, auto& y) { return x.count > y.count; });

        // home chord: the most common chord built on the home note, otherwise the most common chord
        a.homeChord = a.chordShares.empty() ? 0 : a.chordShares.front().mask;
        for (auto& c : a.chordShares)
            if (chordRoot (c.mask, a.voicing[c.mask].front() % 12) == a.home % 12) { a.homeChord = c.mask; break; }

        // repeating chord phrase
        a.chordPeriodBars = a.bars;
        for (int p = 1; p < a.bars; ++p)
        {
            if (a.bars % p != 0) continue;
            auto block = [&] (int startBar)
            {
                std::set<int> on;
                std::multiset<int> masks;
                for (auto& c : a.chords)
                    if (c.step >= startBar * a.stepsPerBar && c.step < (startBar + p) * a.stepsPerBar)
                    { on.insert (c.step - startBar * a.stepsPerBar); masks.insert (c.mask); }
                return std::make_pair (on, masks);
            };
            const auto first = block (0);
            if (first.first.empty()) continue;
            bool repeats = true;
            for (int k = p; k < a.bars && repeats; k += p)
            {
                const auto other = block (k);
                std::vector<int> both, either, common;
                std::set_intersection (first.first.begin(), first.first.end(), other.first.begin(), other.first.end(), std::back_inserter (both));
                std::set_union (first.first.begin(), first.first.end(), other.first.begin(), other.first.end(), std::back_inserter (either));
                std::set_intersection (first.second.begin(), first.second.end(), other.second.begin(), other.second.end(), std::back_inserter (common));
                repeats = ! either.empty() && (double) both.size() / either.size() >= 0.75
                          && (double) common.size() / std::max (first.second.size(), other.second.size()) >= 0.6;
            }
            if (repeats) { a.chordPeriodBars = p; break; }
        }
        for (auto& c : a.chords)
            if (c.step < a.chordPeriodBars * a.stepsPerBar)
            {
                auto e = c;
                e.length = std::min (e.length, a.chordPeriodBars * a.stepsPerBar - e.step);
                a.chordPhrase.push_back (e);
            }
    }

    // Bars with identical notes and rhythm (used by Stab / Riff to keep motifs)
    {
        std::map<std::vector<std::array<int, 3>>, int> sigToGroup;
        for (int b = 0; b < a.bars; ++b)
        {
            std::vector<std::array<int, 3>> sig;
            for (auto& r : raw)
                if (r.step / a.stepsPerBar == b) sig.push_back ({ r.step % a.stepsPerBar, r.len, r.pitch });
            auto it = sigToGroup.find (sig);
            if (it == sigToGroup.end()) it = sigToGroup.emplace (sig, (int) sigToGroup.size()).first;
            a.barGroup.push_back (it->second);
        }
    }

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
    int filled = 0, shortNotes = 0;
    bool allShort = true;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        const auto& r = raw[i];
        meanPitch += r.pitch;
        filled += r.len;
        allShort = allShort && r.len <= 2;
        // short (an 8th or less) and detached (silence before the next note)
        const int toNext = i + 1 < raw.size() ? raw[i + 1].step - r.step : r.len + 1;
        shortNotes += (r.origLength <= 0.5 + 1e-6 && r.origLength < toNext * 0.25 * 0.97) ? 1 : 0;
    }
    meanPitch /= raw.size();
    const double fill = (double) filled / (a.bars * a.stepsPerBar);
    const std::set<int> groups (a.barGroup.begin(), a.barGroup.end());
    const bool barsRepeat = (int) groups.size() < a.bars;
    const bool stabby = (a.highest - a.lowest) <= 7 && shortNotes >= 0.7 * raw.size() && (barsRepeat || a.bars == 1);

    if (a.chordRatio >= 0.5)             a.guessedType = MidiType::chords;   // mostly 2+ notes at once
    else if (meanPitch < 52.0)           a.guessedType = MidiType::bass;     // below E2 (Ableton E1-ish)
    else if (allShort && fill >= 0.6)    a.guessedType = MidiType::arp;
    else if (stabby)                     a.guessedType = MidiType::stab;
    else                                 a.guessedType = MidiType::melody;

    a.valid = true;
    return a;
}

void varyFrom (int ref, const Settings& s, LaneSet& lanes, std::uint32_t seed)
{
    if (ref < 0 || ref >= numLanes || lanes[(size_t) ref].empty) return;
    Rng rng (seed);

    // the reference lane as events: notes starting together move together (chords stay whole)
    std::map<long, std::vector<Note>> byStep;
    for (auto& n : lanes[(size_t) ref].notes) byStep[std::lround (n.start * 4.0)].push_back (n);
    std::vector<std::vector<Note>> events;
    for (auto& kv : byStep) events.push_back (kv.second);
    const int n = (int) events.size();
    if (n < 2) return;

    auto content = [] (const std::vector<Note>& e)
    {
        std::vector<int> ps;
        for (auto& x : e) ps.push_back (x.pitch);
        std::sort (ps.begin(), ps.end());
        return ps;
    };
    const int swaps = std::max (1, (int) std::lround (n * (0.04 + 0.02 * s.variation)));

    for (int li = 0; li < numLanes; ++li)
    {
        if (li == ref || (lanes[(size_t) li].locked && ! lanes[(size_t) li].empty)) continue;
        std::vector<Note> best;
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            auto ev = events;
            int done = 0;
            for (int tries = 0; tries < 50 * swaps && done < swaps; ++tries)
            {
                const int i = randInt (rng, 0, n - 1), j = randInt (rng, 0, n - 1);
                if (content (ev[(size_t) i]) == content (ev[(size_t) j])) continue;
                if (s.keepShape && i > 0 && j > 0 && std::abs (i - j) == 1) continue;   // don't flip a step's direction
                // swap the notes, keep each position's timing and lengths
                auto pi = content (ev[(size_t) i]), pj = content (ev[(size_t) j]);
                auto assign = [] (std::vector<Note>& e, const std::vector<int>& ps)
                {
                    const auto proto = e.front();
                    e.clear();
                    for (int p : ps) { auto x = proto; x.pitch = p; e.push_back (x); }
                };
                assign (ev[(size_t) i], pj);
                assign (ev[(size_t) j], pi);
                ++done;
            }
            std::vector<Note> notes;
            for (auto& e : ev) for (auto& x : e) notes.push_back (x);

            bool unique = overlap (lanes[(size_t) ref].notes, notes) < 0.999;
            for (int other = 0; other < li && unique; ++other)
                if (other != ref && ! lanes[(size_t) other].empty) unique = overlap (lanes[(size_t) other].notes, notes) < 0.999;
            for (int other = li + 1; other < numLanes && unique; ++other)
                if (lanes[(size_t) other].locked && ! lanes[(size_t) other].empty) unique = overlap (lanes[(size_t) other].notes, notes) < 0.999;
            best = notes;
            if (unique) break;
        }
        lanes[(size_t) li].notes = best;
        lanes[(size_t) li].newNoteCount = lanes[(size_t) ref].newNoteCount;
        lanes[(size_t) li].empty = best.empty();
    }
}

void generate (const Analysis& a, const Settings& s, LaneSet& lanes, std::uint32_t seed)
{
    if (! a.valid) return;
    Rng rng (seed);

    // Keep Shape follows the source closely, so it's allowed to share a few more notes with it
    const double baseLimit = s.maxSourceOverlap >= 0.0 ? s.maxSourceOverlap
                           : (s.rhythm == RhythmMode::locked ? 0.5 : 0.35) + (s.keepShape ? 0.1 : 0.0);
    const LaneSet previous = lanes;   // the batch before this Generate: don't hand those back again

    for (int li = 0; li < numLanes; ++li)
    {
        auto& lane = lanes[(size_t) li];
        if (lane.locked && ! lane.empty) continue;

        std::vector<Note> best;
        double bestScore = 1e9;
        int bestNew = 0;

        for (int attempt = 0; attempt < 400; ++attempt)
        {
            const double chaos = std::clamp ((attempt - 40) / 160.0, 0.0, 1.0);
            int newCount = 0;
            auto notes = s.type == MidiType::stab   ? makeStabClip (a, s, rng, chaos, newCount)
                       : s.type == MidiType::chords ? makeChordClip (a, s, rng, chaos, newCount)
                                                    : makePhraseClip (a, s, rng, chaos, newCount);
            if (notes.empty()) continue;

            const double ovSrc = overlap (s.type == MidiType::chords ? a.polyNotes : a.notes, notes);
            double ovLanes = 0.0;
            for (int other = 0; other < numLanes; ++other)
            {
                if (other == li || other == s.referenceLane) continue;
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
            if (score < bestScore) { bestScore = score; best = notes; lane.overlapWithSource = ovSrc; bestNew = newCount; }
            if (repeatsOld || ovLanes > 0.999) continue;
            if (ovSrc <= limit && ovLanes <= 0.7 + 0.05 * (attempt / 50)) break;
        }

        lane.notes = std::move (best);
        lane.newNoteCount = bestNew;
        lane.empty = lane.notes.empty();
    }
}
} // namespace mc
