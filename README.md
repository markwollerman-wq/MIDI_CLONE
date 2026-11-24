# MIDI Clone (WOLLER) — Phase 3

Drop in a MIDI clip (bass, melody, arp, stab or chords). MIDI Clone measures which notes it uses and how often, then makes **10 new clips** with the same note percentages, the same length and the same rhythmic feel. Preview any of them through your own synth, lock the ones you like, and drag the keeper into Ableton.

![MIDI Clone](screenshot.png)

## How it works

- **Note formula.** The source's notes and their shares are kept exactly. For example, D 50% / Eb 37.5% / Ab 12.5% stays D 50% / Eb 37.5% / Ab 12.5% in every clone. Clones only use notes from the source, so nothing goes higher or lower than your clip.
- **Same length and phrase.** A 4-bar clip gives 4-bar clones. If your clip repeats a 2-bar phrase, the clones repeat a phrase too, with a small change the second time through.
- **Different enough.** Each clone is checked against your clip and against the other lanes, and near-copies are regenerated. "x% like source" on each lane shows how many notes land on the same step and note as your clip.

## Key Check (source pane)

- **Key:** the best-fitting key is picked for you (for example **Auto: C Phrygian**). The menu lists the next best matches with how many notes each would change, and **All keys** has every scale on every home note. Common scales win ties, and the home note is the one your clip lands on (beat 1 of each bar counts extra), not just the most played note.
- **Out-of-key notes** show as hollow white notes, with a warning like "5 of 27 notes out of key".
- **Fix Key** moves each out-of-key note to the nearest in-key note, in the direction the line was already moving (climbing goes up, falling goes down). Timing, length and velocity don't change. Moved notes get a white outline; a dot marks a fixed note that now repeats its neighbour. Your file is never changed: the clones are built from the fixed version. Click **Fixed** again to go back.
- **Move To** shifts the source and the clones to a new home note in the same scale, by the smallest step.
- **Play button** (next to Browse) previews the source through your synth, like the lanes, so you can compare.
- **Drag out the source:** hold on the source piano roll and drag it into Ableton. With Fix Key on you get the fixed version ("testmid1 (C Phrygian fixed)"), with Move To the moved one, otherwise the original.

## Chords

Pick **Chords** in MIDI Type (it's picked automatically when most of the clip is 2+ notes at once). Works for pads and chord stabs.

- Each chord is recognised and named (Cm, Ab, Bbsus4, ...), and the clones keep the **chord percentages**, the chord rhythm and the phrase. Chords that only appear in a later bar (a turnaround) keep their place in the percentages too.
- Progressions start and end on the home chord where your clip does, and follow the chord moves your clip makes.
- **Voice leading:** each chord uses the inversion that moves least from the one before, staying in your clip's range. **Keep Shape** follows the top note (the melody you hear).
- **New Chords** (the New Notes knob in Chords mode) swaps that share of chords for other chords from the key that share notes with the one they replace.

## Controls

| Control | What it does |
|---|---|
| **MIDI Type** | Bass, Melody / Lead, Arp / Pluck or Stab / Riff. Picked automatically when you load a clip, and you can change it. Each type sets its own defaults and rules. Bass keeps the home note on the beat and the high note as a short hit. Melody moves smoothly and resolves home. Arp keeps the pattern moving. Stab / Riff keeps the rhythm locked, notes short, Keep Shape on, and bars that repeat in your clip repeat in the clone. |
| **Rhythm** | **Locked:** your exact rhythm with the notes reshuffled. **Same Feel:** a new rhythm made from the same note lengths. |
| **Engine** | **Grid Snapped:** clean steps. **Slide:** some notes overlap the next one so a synth with glide/portamento slides between them. |
| **Slide** | How many notes glide (Slide mode only). |
| **Keep Shape** | Clones go up where your clip goes up and down where it goes down. |
| **Variation** | 1 = close to your clip, 10 = far from it. |
| **New Notes** | 0–30%. Swaps that share of notes for in-key notes your clip doesn't use, staying close to your notes and range (within 2 semitones). Bass never changes beat 1 notes, and stabs only use neighbouring notes. Each lane shows "+N new". 0% = only your notes. |
| **Match** | **Note count:** each note keeps its share of the notes. **Playing time:** each note keeps its share of how long notes play (a long D counts more than a short D). |
| **Preset** / **Save** | Factory presets (Bass, Rave Stab, Melody, Arp, Chords) and your own. Presets set the options in the top bar, not your clip or lanes. Your presets are saved in `~/Library/WOLLER/MIDI Clone/Presets`. |
| **Generate** | Makes 10 new clips. Locked lanes are kept. |
| **Undo** | Brings back the previous 10 clips. Press again to swap back. |
| **Lane: play button** (or click the lane) | Sends that lane's MIDI out. Only one lane plays at a time, and switching waits for the next bar line. |
| **Lane: lock** | Keeps the lane when you press Generate. |
| **Lane: more like this** (two squares) | Keeps that lane (locks it) and turns the other unlocked lanes into variations of it: a few notes or chords swapped, same rhythm and percentages. Variation sets how many swaps. Undo brings back the previous lanes. |
| **Lane: drag** | Hold on a lane and drag it onto a track or clip slot in Ableton to keep it as a normal MIDI clip. Clips are named with the key, like "testmid1 Clone 3 (C Phrygian)". |

## Set it up in Ableton

1. Put **MIDI Clone** on a MIDI track (Plug-Ins → WOLLER → MIDI Clone). It makes no sound itself.
2. On your synth track (for example Serum 2), set **MIDI From** to the MIDI Clone track and choose **MIDI Clone** in the box under it. Set **Monitor** to **In**.
3. Drop a .mid file onto MIDI Clone's window, or click **Browse**.
4. Press **Generate**, then click lanes to hear them on your synth. Preview follows Ableton's transport; when Ableton is stopped it plays on its own at the set tempo.
5. To keep a lane, drag it onto your synth track (or record the synth track while it previews).

MIDI Clone and its 10 lanes are saved with your Ableton set.

## Get the Mac plugin (GitHub build)

1. Create a new **private** GitHub repository, for example `MidiClone`.
2. Upload everything in this folder (`Source`, `Tools`, `CMakeLists.txt`, `README.md`, `screenshot.png`). The hidden `.github` folder often gets skipped by the browser: add it with **Add file → Create new file**, named `.github/workflows/build.yml`, and paste in the contents of `build.yml`.
3. Open the **Actions** tab and wait for the green check (about 10 minutes).
4. Download **MidiClone-macOS**, unzip it, then double-click **MidiClone-VST3-mac.zip**. If the artifact download fails, the same zip is under **Code → Releases**.
5. Move **MIDI Clone.vst3** into `~/Library/Audio/Plug-Ins/VST3/`, then in Terminal:
   ```
   xattr -cr ~/Library/Audio/Plug-Ins/VST3/"MIDI Clone.vst3"
   ```
6. In Ableton: **Settings → Plug-Ins**, hold **Option** and click **Rescan**. It appears under **Plug-Ins → WOLLER → MIDI Clone**, next to White Noise and Ground Shake.

The build is universal (Apple Silicon and Intel). It's VST3 only, because Audio Units can't send MIDI to other tracks in Ableton.

## Build locally

Requires CMake 3.22+ and a C++17 compiler (Xcode on Mac). JUCE downloads automatically.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Optional test tool: `-DMC_BUILD_TESTS=ON` builds `MidiCloneTest`. Run `MidiCloneTest clip.mid outfolder [expected_fixed.mid]`: it generates clones, checks the note percentages, range, length and uniqueness, lock behaviour, the MIDI output against a fake 140 BPM transport (timing and the switch on the bar line), saving and reopening, Key Check, New Notes, Keep Shape, Undo, source preview and Move To, and saves a screenshot.

Phase 3 checks: chord percentages, voice leading, New Chords in key, source export (original and fixed), More like this, Match by playing time, and presets.

`Tools/testdata` has test clips (your SICK_D bassline and testmid1 riff, a pad progression and a chord-stab riff) and the reference riff: `MidiCloneTest Tools/testdata/testmid1.mid out Tools/testdata/testmid1_C_phrygian.mid` checks that the riff is detected as C Phrygian / Stab, that Fix Key turns it into exactly the corrected file, and that repeated bars stay repeated.

## Not included

The optional Max for Live helper (writing clips straight into your set) isn't built: it needs Ableton Suite / Max for Live and can't be tested here. Dragging lanes and the source out covers the same job.

## Licence note

Built with JUCE (AGPLv3 or JUCE's commercial licences). Fine for personal use; check JUCE's licence before selling it.
