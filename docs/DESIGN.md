# LAB X-19 design notes

## Sonic identity

Laboratory X-19 lies inside the Chornobyl NPP (the *S.T.A.L.K.E.R. 2* Interactive Almanac separates it from X-10, which
*Shadow of Chornobyl*'s diaries also call "X19"). It hosts C-Consciousness and the Wish Granter hologram; Strelok chooses
to join them or destroy them. The plugin takes the hopeful reading: many voices that choose each other, an uncertain
future with endless possibilities.

Guiding rules:

1. **Clean chords, gritty undertone.** Grit is added in parallel above a corner, never under it.
2. **Many voices as one.** Unison copies, choir singers, chord notes and strummed strings.
3. **Built for the piano roll.** One key plays a chord; STRUM and ARP are sample accurate and lock to the host grid.
4. **DAWN clears the haze.** Filter +1.5 octaves, vowel +0.25, shimmer +0.4, reverb mix +0.15, ensemble +0.25,
   air level x1.6, grit and dust x0.4.

## Signal flow

Per voice (16 voices, one per sounding note):

```
OSC A x BONDS (1..7) ─┐
OSC B, SUB, NOISE ────┤
CHOIR (1..8 singers ─> 4 formants per side) ─┤
SPECIMEN (Grain cloud | Strike one-shot) ─┤
AIR (grain cloud) ─┴─> drive ─> FILTER (ladder | SVF LP/BP/HP) ─> AMP ENV
```

Note engine: held keys ─> CHORD (shape, voicing) ─> STRUM-delayed starts, or ARP steps ─> voice allocation
(poly with stealing, mono, legato). Voices are addressed by tag (key x 8 + chord index; arp steps from 1024).

Global, per control block (at most 16 samples, cut at every scheduled event):

```
voices ─> LFO level ─> GRIT ─> ENSEMBLE ─> DELAY ─> REVERB + SHIMMER ─> width ─> master ─> DC blocker
       ─> REGULATOR ─> fault guard ─> preset fade ─> safety clip
```

## Modules

- **Note engine** (`DSP/NoteEngine.h`): an absolute sample clock and a fixed queue of 256 pending events. Note-offs
  fire before note-ons at the same sample. ARP steps sit at `index x step` beats, odd steps pushed late by
  `0.5 x swing x step`. When synced, the beat is FL's ppq; a key pressed between steps waits for the next one; a host
  loop or jump resynchronises to the next grid point. Unsynced, an internal beat clock starts at the first key. At
  full gate the previous step ends one sample after the next begins, so Legato slides. Mono modes ignore CHORD
  unless the arp is on.
- **CHOIR** (`DSP/Choir.h`): per singer a PolyBLEP saw through a one-pole low-pass at 10 x f0 (1.5 to 8 kHz), detune
  spread, jitter, a slow random drift up to 6 cents, vibrato 4.6 to 5.8 Hz up to 40 cents fading in after 0.15 s.
  Singers alternate sides (an odd count puts the middle one in the centre). Four TPT band-passes per side; alto
  formants from the Csound manual table, bandwidths x1.6, interpolated U-O-A-E-I (log frequency). The table gives
  output peak levels, so each formant is lifted by fc / F1 (at most +21 dB) to undo the saw's 6 dB/octave slope, and
  each vowel is normalised by F1 / sqrt(sum(level^2 x bandwidth)) so loudness stays within about 5 dB across vowels.
- **SPECIMEN** (`SpecimenLibrary`, `DSP/Granular.h`): each load keeps two copies. The Strike copy keeps the recording's
  dynamics, levelled on its loudest 400 ms; Strike starts 2 ms before the signal first reaches 5 % of its peak. The
  Grain copy has its slow contour flattened (LAB X-3) and feeds the wavetable cutter (LAB X-10). Playback ratio is
  `2^((keytrack x (note - root) + tune) / 12)`; the root is the catalogue's note (pitch class in octave 3), or for a
  user file the detected table pitch. Grains get a random place in the stereo field. Burst guard as in X-3/X-10.
- **GRIT** (`DSP/Effects.h`): a 12 dB/oct high-pass at CORNER, drive, shaper (asymmetric tanh, tanh, hard clip, or
  sample-and-hold plus bit reduction), output compensation `0.5 / tanh (0.5 g)`, a second high-pass at CORNER, then
  a 9 kHz (tape 6.5 kHz, radio 3 kHz) two-pole low-pass. DUST: crackle impulses (rate dust^2 x 40/s) and hiss, both
  scaled by an envelope follower of the input.
- **ENSEMBLE**: three taps per side at 8 ms + depth x 6 ms, swept by phase-offset sines (right side half a cycle
  later) plus a 6.3 Hz shimmer of +/-0.12 ms.
- **DELAY**: two 2.6 s lines; the time glides to its target (about 50 ms); the feedback path is low-passed at TONE,
  high-passed at 120 Hz and soft-limited.
- **REVERB**: LAB X-3's NOOSPHERE FDN plus a 0-250 ms pre-delay. SHIMMER: the mono wet signal into an 8192-sample
  buffer read by two heads 60 ms long sliding at double speed with sin^2 crossfades (an octave up), band-passed
  300 Hz to 6 kHz, soft-limited and fed back into the network's input at up to 0.55. Each pass climbs an octave into
  the damping and the 6 kHz low-pass, so even decay 1 and shimmer 1 die away (test 9: no 1 s window louder than the
  one before, -95 dB after 28 s).
- **REGULATOR**: LAB X-10's compressor and look-ahead limiter (latency 71 samples at 48 kHz); the WEIGHT shelf is unused.
- **Hardening** (from X-3/X-10): preset changes write every parameter once, fade out over 30 ms, flush voices, note
  engine and every effect, snap smoothers, fade in. Non-finite output is silenced and counted as FAULTS. Each source
  layer ducks out while a new file loads.

## Presets

19 presets, loudness matched to -14 LUFS maximum momentary loudness (one key for chord and arp presets, three keys
otherwise), peaks capped at -1 dBFS. Skadovsk Stab sits near -16 because its transient reaches the cap first.

## Verification (0.1.0)

`python Tests/run_tests.py`: 33/33 checks, including pitch within 0.05 cents, exact chord intervals, strum and arp
timing within 0.06 ms, a 300 s fuzz session with no faults, and 7x real time with 16 voices and everything on.
pluginval strictness 10 passes.
