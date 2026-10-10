"""Automated offline checks for LAB X-19.

Runs the LabX19Render harness through a battery of renders and verifies them with analyze.py.
Usage:  python Tests/run_tests.py [--specimens <STALKER SPECIMENS folder>] [--only 1,2,5] [--skip-pluginval]
                                  [--fuzz-seconds 300]
Without --specimens the harness finds the library itself, as the plugin does.

Writes WAVs to Tests/out (ignored by git) and a JSON report to build/test-report.json.
Test numbers follow the plan in LAB_X19_HANDOFF.md section 5.
"""

import argparse
import json
import math
import pathlib
import re
import shutil
import subprocess
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
HARNESS = ROOT / "build" / "LabX19Render_artefacts" / "Release" / "LabX19Render.exe"
BUNDLE = ROOT / "build" / "LabX19_artefacts" / "Release" / "VST3" / "LAB X-19.vst3"
OUT = ROOT / "Tests" / "out"
sys.path.insert(0, str(ROOT / "Tests"))
import analyze as an  # noqa: E402

# A neutral patch: one pure sine, no modulation, no effects, no regulation.
CLEAN = ["osc_a_wave=3", "osc_a_shape=0", "osc_a_level=1", "uni_voices=1", "osc_b_level=0", "sub_level=0",
         "noise_level=0", "choir_level=0", "specimen_level=0", "air_level=0",
         "filter_model=1", "filter_cutoff=20000", "filter_res=0", "filter_drive=0", "filter_env=0",
         "filter_keytrack=0", "filter_vel=0", "env1_attack=0.001", "env1_decay=0.1", "env1_sustain=1",
         "env1_release=0.02", "amp_vel=0", "chord_type=0", "strum_time=0", "arp_mode=0", "lfo_depth=0",
         "grit_amount=0", "grit_dust=0", "ens_mix=0", "delay_mix=0", "delay_feedback=0", "rev_mix=0",
         "rev_shimmer=0", "reg_amount=0", "dawn=0", "master_volume=-6", "voice_mode=0", "glide=0", "width=1"]

STAT = re.compile(r'(\w+)=("([^"]*)"|\S+)')
SPECIMENS = None


def render(name, extra, timeout=600):
    OUT.mkdir(parents=True, exist_ok=True)
    wav = OUT / f"{name}.wav"
    cmd = [str(HARNESS), "--out", str(wav)] + extra
    if SPECIMENS:
        cmd += ["--specimens", SPECIMENS]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    stats = {}
    for line in proc.stdout.splitlines():
        for key, value, quoted in STAT.findall(line):
            stats[key] = quoted if value.startswith('"') else value
    stats["exit"] = proc.returncode
    if proc.returncode != 0:
        stats["stderr"] = (proc.stderr.strip() or proc.stdout.strip())[-400:]
    return wav, stats


def sets(values):
    out = []
    for v in values:
        out += ["--set", v]
    return out


def hz(note):
    return 440.0 * 2 ** ((note - 69) / 12)


def spectral_peaks(x, sr, lo=40.0, hi=5000.0, rel_db=-30.0):
    """Frequencies of local spectral maxima within rel_db of the largest, from one long FFT."""
    n = 1 << int(math.floor(math.log2(len(x))))
    spec = np.abs(np.fft.rfft(x[:n] * np.hanning(n)))
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    sel = (freqs >= lo) & (freqs <= hi)
    s = spec[sel]
    f = freqs[sel]
    top = s.max()
    out = []
    for i in range(1, len(s) - 1):
        if s[i] >= s[i - 1] and s[i] > s[i + 1] and 20 * math.log10(s[i] / top + 1e-30) >= rel_db:
            out.append(float(f[i]))
    return out


def tone_onset(x, sr, freq, frame_s=0.002, threshold=0.5):
    """Time at which the magnitude of one frequency first reaches threshold x its maximum (sliding DFT).
    A step reaches half its final magnitude when the Hann window's centre crosses it."""
    frame = int(0.06 * sr)
    hop = int(frame_s * sr)
    t = np.arange(frame) / sr
    probe = np.exp(-2j * math.pi * freq * t) * np.hanning(frame)
    mags = np.array([abs(np.dot(x[i:i + frame], probe)) for i in range(0, len(x) - frame, hop)])
    k = int(np.argmax(mags >= threshold * mags.max()))
    return (k * hop + frame / 2) / sr


def onsets_from_envelope(x, sr, threshold_db=-30.0, min_gap=0.04):
    """Rising crossings of the analytic-signal envelope (1 ms resolution), at least min_gap apart."""
    from scipy.signal import hilbert
    mag = np.abs(hilbert(np.asarray(x, dtype=float)))
    hop = max(1, int(0.001 * sr))
    env = mag[:len(mag) // hop * hop].reshape(-1, hop).mean(axis=1)
    ref = env.max()
    on = env > ref * 10 ** (threshold_db / 20)
    times = []
    last = -1.0
    for i in range(0, len(on)):
        if on[i] and (i == 0 or not on[i - 1]):
            t = i * 0.001
            if t - last >= min_gap:
                times.append(t)
                last = t
    return times


def attack_time(x, sr, around, before=0.03, after=0.05, frac=0.05):
    """First sample, near an expected time, where the level reaches frac of the local peak."""
    a = np.abs(np.asarray(x))
    lo = max(0, int((around - before) * sr))
    seg = a[lo:int((around + after) * sr)]
    return (lo + int(np.argmax(seg > frac * seg.max()))) / sr


def main():
    global SPECIMENS
    parser = argparse.ArgumentParser()
    parser.add_argument("--specimens")
    parser.add_argument("--only", help="comma-separated test numbers")
    parser.add_argument("--skip-pluginval", action="store_true")
    parser.add_argument("--fuzz-seconds", type=float, default=300.0)
    args = parser.parse_args()
    SPECIMENS = args.specimens

    if not HARNESS.exists():
        print(f"harness not built: {HARNESS}")
        return 2

    only = {int(x) for x in args.only.split(",")} if args.only else None
    presets = [line.split("\t")[1] for line in
               subprocess.run([str(HARNESS), "--list"], capture_output=True, text=True).stdout.splitlines() if "\t" in line]
    catalogue = [line.split("\t")[1] for line in
                 subprocess.run([str(HARNESS), "--list-specimens"], capture_output=True, text=True).stdout.splitlines()
                 if "\t" in line]
    report = {"tests": []}

    def want(n):
        return only is None or n in only

    def record(number, name, ok, detail):
        report["tests"].append({"number": number, "name": name, "pass": bool(ok), **detail})
        print(f"{'PASS' if ok else 'FAIL'}  {number:2d} {name}  {json.dumps(detail)[:360]}")

    def load_mono(wav):
        data, sr = an.load(str(wav))
        return an.mono(data), data, sr

    # 1 + 21. Pitch accuracy: A4 must render at 440 Hz within 1 cent at 44.1, 48 and 96 kHz.
    for sr in (48000, 44100, 96000):
        number = 1 if sr == 48000 else 21
        if want(number):
            wav, st = render(f"pitch_a4_{sr}", ["--sr", str(sr), "--notes", "69", "--seconds", "2", "--hold", "1.8"] + sets(CLEAN))
            m, _, fs = load_mono(wav)
            f0 = an.dominant_frequency(an.segment(m, fs, 0.2, 1.7), fs)
            cents = 1200 * math.log2(f0 / 440.0)
            record(number, f"pitch A4 = 440 Hz at {sr}", abs(cents) <= 1.0 and st["exit"] == 0,
                   {"f0": round(f0, 4), "cents": round(cents, 3)})

    # 2. CHORD: every shape sounds exactly its intervals; voicings move the right notes.
    if want(2):
        cases = [("Major", 1, 0, [0, 4, 7]), ("Min7", 7, 0, [0, 3, 7, 10]), ("Maj9", 9, 0, [0, 4, 7, 11, 14]),
                 ("Lydian", 15, 0, [0, 4, 7, 11, 18]), ("Min11", 12, 0, [0, 3, 7, 10, 14, 17]),
                 ("Maj7 Open", 6, 1, [0, 16, 7, 23]), ("Major Wide", 1, 2, [-12, 4, 19]), ("Maj7 Drop 2", 6, 3, [0, 4, -5, 11])]
        for label, shape, voicing, intervals in cases:
            wav, st = render(f"chord_{shape}_{voicing}", ["--notes", "48", "--seconds", "2", "--hold", "1.8", "--velocity", "1"]
                             + sets(CLEAN + [f"chord_type={shape}", f"chord_voicing={voicing}", "master_volume=-18"]))
            m, _, fs = load_mono(wav)
            peaks = spectral_peaks(an.segment(m, fs, 0.2, 1.7), fs, 40, 2000, -40)
            expected = sorted(hz(48 + i) for i in intervals)
            missing = [round(e, 1) for e in expected if not any(abs(p / e - 1) < 0.004 for p in peaks)]
            extra = [round(p, 1) for p in peaks if not any(abs(p / e - 1) < 0.004 for e in expected)]
            record(2, f"chord {label}: exactly its notes", not missing and not extra and st["exit"] == 0,
                   {"expected": [round(e, 1) for e in expected], "missing": missing, "extra": extra})

    # 3. STRUM: notes start STRUM ms apart, low to high for Down, high to low for Up.
    if want(3):
        for direction, label in ((0, "down"), (1, "up")):
            wav, st = render(f"strum_{label}", ["--events", "48@0.2-1.4", "--seconds", "1.6"]
                             + sets(CLEAN + ["chord_type=13", "strum_time=50", f"strum_dir={direction}", "master_volume=-18"]))
            m, _, fs = load_mono(wav)
            times = [tone_onset(m, fs, hz(48 + i)) - 0.2 for i in (0, 7, 14)]
            gaps = [times[1] - times[0], times[2] - times[1]]
            expect = 0.05 if direction == 0 else -0.05
            ok = all(abs(g - expect) <= 0.003 for g in gaps) and st["exit"] == 0
            record(3, f"strum {label}: 50 ms between notes", ok, {"onsets_ms": [round(1000 * t, 1) for t in times]})

    # 4. ARP on the host grid: 1/16 at 120 BPM is 125 ms; Up order; swing pushes odd steps late.
    if want(4):
        for swing in (0.0, 0.4):
            wav, st = render(f"arp_swing_{swing}", ["--notes", "48", "--seconds", "1.6", "--hold", "1.5", "--bpm", "120"]
                             + sets(CLEAN + ["chord_type=1", "arp_mode=1", "arp_rate=3", "arp_gate=0.5",
                                             f"arp_swing={swing}", "arp_octaves=2", "master_volume=-12"]))
            m, _, fs = load_mono(wav)
            expected_times = [k * 0.125 + (0.5 * swing * 0.125 if k % 2 else 0.0) for k in range(8)]
            ons = [attack_time(m, fs, t) for t in expected_times]
            notes = []
            for k in range(8):
                t0 = expected_times[k] + 0.01
                f0 = an.dominant_frequency(an.segment(m, fs, t0, t0 + 0.045), fs, 60)
                notes.append(round(69 + 12 * math.log2(f0 / 440.0)))
            want_notes = [48, 52, 55, 60, 64, 67, 48, 52]
            timing = max(abs(a - b) for a, b in zip(ons, expected_times)) if len(ons) == 8 else 1.0
            ok = len(ons) == 8 and timing <= 0.002 and notes == want_notes and st["exit"] == 0
            record(4, f"arp up 1/16 at 120 BPM, swing {swing}", ok,
                   {"onsets_ms": [round(1000 * t, 1) for t in ons], "notes": notes, "worst_error_ms": round(1000 * timing, 2)})

        # A key pressed between steps waits for the next step on the grid.
        wav, st = render("arp_offgrid", ["--events", "60@0.05-1.0", "--seconds", "1.2", "--bpm", "120"]
                         + sets(CLEAN + ["arp_mode=1", "arp_rate=3", "arp_gate=0.5", "master_volume=-12"]))
        m, _, fs = load_mono(wav)
        first = np.argmax(np.abs(m) > 1e-4) / fs
        record(4, "arp: a key pressed off the grid starts on the next step", abs(first - 0.125) <= 0.001,
               {"first_onset_ms": round(1000 * first, 2)})

    # 5. BONDS: seven detuned saws spread the spectrum and the stereo field.
    if want(5):
        wav, st = render("bonds", ["--notes", "69", "--seconds", "3", "--hold", "3"]
                         + sets(CLEAN + ["osc_a_wave=0", "uni_voices=7", "uni_detune=30", "uni_spread=1", "master_volume=-12"]))
        _, data, fs = load_mono(wav)
        seg = data[int(0.3 * fs):int(2.9 * fs)]
        side = 0.5 * (seg[:, 0] - seg[:, 1])
        mid = 0.5 * (seg[:, 0] + seg[:, 1])
        width_db = an.db(math.sqrt(np.mean(side ** 2))) - an.db(math.sqrt(np.mean(mid ** 2)))
        spread = an.peak_spread(mid, fs, 440.0, 12.0)
        record(5, "bonds: >= 10 Hz spread at 440 Hz, side within 10 dB of mid", spread >= 10.0 and width_db > -10.0,
               {"spread_hz": round(spread, 2), "side_vs_mid_db": round(width_db, 1)})

    # 6. CHOIR: vowels move the formants (U darkest, E and I with a high second formant), level near one saw.
    if want(6):
        saw_wav, _ = render("choir_ref_saw", ["--notes", "57", "--seconds", "2", "--hold", "2"]
                            + sets(CLEAN + ["osc_a_wave=0", "osc_a_level=0.8"]))
        saw_rms = an.rms_db(an.segment(load_mono(saw_wav)[0], 48000, 0.5, 1.9))
        results = {}
        for vowel in (0.0, 0.25, 0.5, 0.75, 1.0):
            wav, st = render(f"choir_{vowel}", ["--notes", "57", "--seconds", "2", "--hold", "2"]
                             + sets(CLEAN + ["osc_a_level=0", "choir_level=0.8", f"choir_vowel={vowel}", "choir_breath=0"]))
            m, _, fs = load_mono(wav)
            seg = an.segment(m, fs, 0.5, 1.9)
            freqs, p = an.power_spectrum(seg, fs)
            low = p[(freqs > 150) & (freqs < 1200)].sum()
            f2 = p[(freqs > 1400) & (freqs < 2400)].sum()
            first = freqs[(freqs > 150) & (freqs < 1300)][np.argmax(p[(freqs > 150) & (freqs < 1300)])]
            results[vowel] = {"rms_db": round(an.rms_db(seg), 1), "f2_ratio_db": round(10 * math.log10(f2 / low + 1e-30), 1),
                              "peak_hz": round(float(first))}
        ok = (results[0.75]["f2_ratio_db"] > results[0.25]["f2_ratio_db"] + 6
              and results[1.0]["f2_ratio_db"] > results[0.0]["f2_ratio_db"] + 6
              and results[0.5]["peak_hz"] > results[0.0]["peak_hz"]
              and all(abs(r["rms_db"] - saw_rms) <= 5.0 for r in results.values()))
        record(6, "choir: vowels move the formants, level within 5 dB of a saw", ok, {"saw_rms_db": round(saw_rms, 1), **{str(k): v for k, v in results.items()}})

    # 7. FILTER: slopes and resonance from white noise.
    if want(7):
        noise = CLEAN + ["osc_a_level=0", "noise_level=0.5", "noise_colour=1", "master_volume=-12", "filter_cutoff=500"]
        dry_wav, _ = render("filter_dry", ["--notes", "60", "--seconds", "4", "--hold", "4"]
                            + sets(noise + ["filter_model=1", "filter_cutoff=20000"]))
        dry = an.mono(an.load(str(dry_wav))[0])[4800:]
        limits = {0: ("ladder", 20, 28), 1: ("svf", 10, 14)}
        for model, (label, lo, hi) in limits.items():
            wav, st = render(f"filter_{label}", ["--notes", "60", "--seconds", "4", "--hold", "4"]
                             + sets(noise + [f"filter_model={model}", "filter_res=0"]))
            m = an.mono(an.load(str(wav))[0])[4800:]
            slope = -an.slope_db_per_octave(m, dry, 48000, 1000, 4000)
            wav, st2 = render(f"filter_{label}_res", ["--notes", "60", "--seconds", "4", "--hold", "4"]
                              + sets(noise + [f"filter_model={model}", "filter_res=0.85"]))
            m2 = an.mono(an.load(str(wav))[0])[4800:]
            peak = an.response_peak(m2, dry, 48000, 150, 2000)
            ok = lo <= slope <= hi and abs(peak / 500.0 - 1.0) <= 0.10 and st["exit"] == 0
            record(7, f"filter {label}: slope {lo}-{hi} dB/oct, peak within 10 %", ok,
                   {"slope_db_oct": round(slope, 1), "res_peak_hz": round(peak, 1)})

    # 8. GRIT: the band below the corner is untouched, harmonics appear above it.
    if want(8):
        base = CLEAN + ["chord_type=1", "master_volume=-18"]
        res = {}
        for amount in (0, 1):
            wav, st = render(f"grit_{amount}", ["--notes", "48", "--seconds", "2", "--hold", "2"]
                             + sets(base + ["grit_mode=0", f"grit_amount={amount}", "grit_drive=24", "grit_tone=1200"]))
            m, _, fs = load_mono(wav)
            seg = an.segment(m, fs, 0.3, 1.8)
            res[amount] = (an.band_energy(seg, fs, 60, 420), an.band_energy(seg, fs, 1500, 8000))
        low_change = 10 * math.log10(res[1][0] / res[0][0])
        high_gain = 10 * math.log10(res[1][1] / (res[0][1] + 1e-30))
        record(8, "grit: low band within 0.5 dB, > 10 dB more above the corner", abs(low_change) <= 0.5 and high_gain > 10,
               {"low_change_db": round(low_change, 2), "high_gain_db": round(high_gain, 1)})

    # 9. SHIMMER REVERB: the tail climbs an octave and still dies away, even at full shimmer and decay.
    if want(9):
        wav, st = render("shimmer", ["--notes", "69", "--hold", "0.3", "--seconds", "14", "--tail-from", "1.0"]
                         + sets(CLEAN + ["rev_mix=1", "rev_size=0.8", "rev_decay=0.7", "rev_shimmer=0.8", "master_volume=-12"]))
        m, _, fs = load_mono(wav)
        tail = an.segment(m, fs, 1.5, 4.0)
        freqs, p = an.power_spectrum(tail, fs)
        e440 = p[(freqs > 430) & (freqs < 450)].sum()
        e880 = p[(freqs > 860) & (freqs < 900)].sum()
        octave_db = 10 * math.log10(e880 / (e440 + 1e-30) + 1e-30)
        late = an.rms_db(an.segment(m, fs, 12.0, 14.0))
        early = an.rms_db(an.segment(m, fs, 1.0, 2.0))
        wav2, st2 = render("shimmer_max", ["--notes", "69", "--hold", "0.3", "--seconds", "30", "--tail-from", "1.0"]
                           + sets(CLEAN + ["rev_mix=1", "rev_size=1", "rev_decay=1", "rev_shimmer=1", "rev_damping=0",
                                           "master_volume=-12"]))
        # The shimmer's crossfading heads make the level wobble within ~100 ms, so growth is judged on 1 s windows.
        m2, _, _ = load_mono(wav2)
        secs = [an.rms_db(an.segment(m2, fs, t, t + 1.0)) for t in range(1, 29)]
        rise = round(max(b - a for a, b in zip(secs, secs[1:])), 2)
        ok = octave_db > -12 and late < early - 20 and rise < 0.5 and secs[-1] < secs[0] - 40
        record(9, "shimmer: octave in the tail, decays, never grows", ok,
               {"octave_vs_fundamental_db": round(octave_db, 1), "early_db": round(early, 1), "late_db": round(late, 1),
                "max_settings_rise_db": rise, "max_settings_peak_db": st2.get("tail_peak_db")})

    # 10. DELAY: a 1/8 echo at 120 BPM arrives after 250 ms.
    if want(10):
        wav, st = render("delay", ["--events", "69@0.1-0.15", "--seconds", "1.2", "--bpm", "120"]
                         + sets(CLEAN + ["delay_mix=1", "delay_sync=2", "delay_feedback=0", "delay_mode=0",
                                         "env1_release=0.005", "master_volume=-12"]))
        m, _, fs = load_mono(wav)
        ons = onsets_from_envelope(m, fs, -40.0, 0.1)
        gap = ons[1] - ons[0] if len(ons) >= 2 else 0.0
        record(10, "delay: 1/8 at 120 BPM = 250 ms", abs(gap - 0.25) <= 0.002, {"onsets_ms": [round(1000 * t, 1) for t in ons[:4]]})

    # 11. STRIKE: the campfire A chord plays at its recorded pitch on its root key and an octave up on the key above.
    if want(11):
        res = {}
        for key in (57, 69):
            wav, st = render(f"strike_{key}", ["--notes", str(key), "--seconds", "3", "--hold", "2.8"]
                             + sets(CLEAN + ["osc_a_level=0", "specimen_source=1", "specimen_mode=1", "specimen_level=1"]))
            m, _, fs = load_mono(wav)
            seg = an.segment(m, fs, 0.05, 2.0)
            res[key] = (an.dominant_frequency(seg, fs, 100), an.rms_db(seg), st.get("specimen", ""))
        ratio = res[69][0] / res[57][0]
        ok = abs(ratio / 2.0 - 1) < 0.01 and abs(res[57][0] / 278.6 - 1) < 0.02 and res[57][1] > -40
        record(11, "strike: root key plays the recording, +12 doubles the pitch", ok,
               {"f_root": round(res[57][0], 1), "f_octave": round(res[69][0], 1), "rms_db": round(res[57][1], 1),
                "specimen": res[57][2]})

    # 12. Every catalogue source loads and plays at a sane level in Grain and Strike modes.
    if want(12):
        bad = []
        levels = {}
        for i, name in enumerate(catalogue, start=1):
            for mode in (0, 1):
                wav, st = render(f"source_{i:02d}_{mode}", ["--notes", "57", "--seconds", "1.5", "--hold", "1.4"]
                                 + sets(CLEAN + ["osc_a_level=0", f"specimen_source={i}", f"specimen_mode={mode}",
                                                 "specimen_level=0.8", "specimen_position=0.3" if mode == 0 else "specimen_position=0"]))
                rms = float(st.get("rms_db", "-120"))
                ready = " S / " in st.get("specimen", "")
                levels[f"{i}{'GS'[mode]}"] = rms
                if st["exit"] != 0 or not ready or rms < -50 or float(st.get("peak_db", "0")) > 0.0:
                    bad.append((name, mode, st.get("specimen"), rms))
        record(12, f"all {len(catalogue)} sources load and play in both modes", not bad,
               {"failures": bad[:6], "rms_range_db": [round(min(levels.values()), 1), round(max(levels.values()), 1)]})

    # 13. Every preset: finite, under the ceiling, and loud enough to hear.
    if want(13):
        failures, loud = [], {}
        for i, name in enumerate(presets):
            wav, st = render(f"preset_{i:02d}", ["--preset", str(i), "--notes", "48,55,64", "--seconds", "4", "--hold", "2.5",
                                                 "--bpm", "120"])
            data, fs = an.load(str(wav))
            level = an.lufs(data[:int(3.2 * fs)], fs, momentary_max=True)   # stabs and pads alike
            loud[name] = round(level, 1)
            if st["exit"] != 0 or int(st.get("non_finite", "1")) != 0 or float(st.get("peak_db", "9")) > -0.2 or level < -24 or level > -6:
                failures.append((name, st.get("peak_db"), round(level, 1)))
        record(13, f"all {len(presets)} presets render cleanly", not failures, {"failures": failures, "momentary_lufs": loud})

    # 14. Preset switching while tails ring: no rise in the silence that follows.
    if want(14):
        n = len(presets)
        changes = ",".join(f"{(k * 5) % n}@{0.6 + 0.35 * k:.2f}" for k in range(1, 9))
        wav, st = render("preset_switch", ["--events", "48@0-0.5,55@0-0.5,64@0-0.5", "--program-at", changes,
                                           "--seconds", "10", "--tail-from", "4.0", "--bpm", "120"])
        rise = float(st.get("tail_rise_db", "99"))
        record(14, "preset switching during tails: no rise", rise < 3.0 and int(st.get("non_finite", "1")) == 0,
               {"tail_rise_db": rise, "tail_peak_db": st.get("tail_peak_db")})

    # 15. State round trip.
    if want(15):
        _, st = render("roundtrip", ["--preset", str(min(3, len(presets) - 1)), "--seconds", "0.2", "--roundtrip"]
                       + sets(["chord_type=9", "arp_mode=3", "choir_vowel=0.8"]))
        record(15, "state round trip", st.get("roundtrip") == "PASS", {"roundtrip": st.get("roundtrip"), "state_bytes": st.get("state_bytes")})

    # 16. Fuzz: random notes, presets and parameter moves with variable block sizes.
    if want(16):
        wav, st = render("fuzz", ["--fuzz", "--seconds", str(args.fuzz_seconds), "--seed", "19", "--vary-blocks"], timeout=3600)
        ok = st["exit"] == 0 and int(st.get("non_finite", "1")) == 0 and int(st.get("growth_fx_only", "1")) == 0
        record(16, f"fuzz {int(args.fuzz_seconds)} s", ok,
               {k: st.get(k) for k in ("events", "preset_changes", "param_changes", "non_finite", "growth_fx_only",
                                       "worst_growth_fx_db", "peak_db", "faults", "block_max_ms")})

    # 17. CPU: 16 voices of chords with everything on.
    if want(17):
        heavy = ["chord_type=12", "uni_voices=7", "osc_b_level=0.5", "choir_level=0.6", "choir_singers=8",
                 "specimen_mode=0", "specimen_level=0.5", "specimen_density=60", "air_level=0.5", "grit_amount=0.5",
                 "grit_dust=0.5", "ens_mix=0.5", "delay_mix=0.3", "rev_mix=0.4", "rev_shimmer=0.5", "filter_model=0",
                 "noise_level=0.2"]
        wav, st = render("cpu", ["--notes", "48,55,62", "--seconds", "6", "--hold", "6"] + sets(heavy))
        rt = float(st.get("realtime_x", "0"))
        record(17, "CPU: 16 voices with everything on renders >= 3x real time", rt >= 3.0,
               {"realtime_x": rt, "block_max_ms": st.get("block_max_ms")})

    # 18. Editor snapshot.
    if want(18):
        png = OUT / "editor.png"
        _, st = render("editor", ["--preset", "1" if len(presets) > 1 else "0", "--notes", "57", "--seconds", "0.5", "--png", str(png)])
        record(18, "editor renders", png.exists() and st["exit"] == 0, {"png": str(png)})

    # 20. Legato slide and the sustain pedal.
    if want(20):
        wav, st = render("legato", ["--events", "57@0-1.0,64@0.8-1.6", "--seconds", "2"]
                         + sets(CLEAN + ["voice_mode=2", "glide=0.1"]))
        m, _, fs = load_mono(wav)
        env = an.envelope(an.segment(m, fs, 0.7, 1.0), fs, 0.002)
        dip = an.db(env.min() / env.max())
        f_end = an.dominant_frequency(an.segment(m, fs, 1.2, 1.55), fs)
        wav2, st2 = render("sustain", ["--events", "57@0-0.3", "--cc", "64:127@0.1,64:0@1.2", "--seconds", "1.6"]
                           + sets(CLEAN))
        m2, _, _ = load_mono(wav2)
        held = an.rms_db(an.segment(m2, fs, 0.8, 1.1))
        after = an.rms_db(an.segment(m2, fs, 1.4, 1.6))
        ok = dip > -3 and abs(1200 * math.log2(f_end / hz(64))) < 5 and held > -30 and after < -60
        record(20, "legato slides without retrigger; pedal holds and releases", ok,
               {"dip_db": round(dip, 2), "f_end": round(f_end, 1), "held_db": round(held, 1), "after_release_db": round(after, 1)})

    # 22. Runaway guard: a blowout in one voice or after any effect stage becomes silence, a FAULT and a log line.
    if want(22):
        log = OUT / "guard_faults.log"
        if log.exists():
            log.unlink()
        bad = []
        for stage in (6, 0, 1, 2, 3, 4):
            wav, st = render(f"guard_{stage}", ["--preset", "0", "--notes", "48,55,64", "--seconds", "3", "--hold", "2.5",
                                                "--inject", f"{stage}@1.5", "--fault-log", str(log)])
            data, fs = an.load(str(wav))
            before = float(np.abs(data[:int(1.45 * fs)]).max())
            after = float(np.abs(data[int(1.45 * fs):]).max())
            if st["exit"] != 0 or int(st.get("faults", "0")) < 1 or int(st.get("non_finite", "1")) != 0 or after > before * 1.05:
                bad.append((stage, st.get("faults"), round(an.db(after), 1)))
        text = log.read_text(encoding="utf-8") if log.exists() else ""
        stages = sorted(set(re.findall(r"FAULT  (\w+)", text)))
        record(22, "runaway guard silences and logs every stage", not bad and len(stages) >= 5,
               {"failures": bad, "logged_stages": stages})

    # 23. Delay lines read exactly, never past the end (the 0.1.1 ENSEMBLE blowout).
    if want(23):
        proc = subprocess.run([str(HARNESS), "--selftest-delay"], capture_output=True, text=True)
        record(23, "delay lines: exact reads at the wrap-around boundary", proc.returncode == 0, {"output": proc.stdout.strip()})

    # 24. The FL session that blew up: C-Consciousness idle with the transport playing, 64-sample blocks, 130.1 BPM.
    if want(24):
        log = OUT / "idle_faults.log"
        if log.exists():
            log.unlink()
        _, st = render("idle_fl_session", ["--preset", "C-Consciousness", "--no-notes", "--seconds", "240", "--block", "64",
                                            "--bpm", "130.1", "--fault-log", str(log)])
        record(24, "idle C-Consciousness for 240 s: no faults", int(st.get("faults", "1")) == 0 and st["exit"] == 0,
               {"faults": st.get("faults"), "peak_db": st.get("peak_db")})

    # 19. pluginval at strictness 10.
    if want(19) and not args.skip_pluginval:
        pluginval = shutil.which("pluginval") or r"F:\_DEV\tools\pluginval\pluginval.exe"
        proc = subprocess.run([pluginval, "--strictness-level", "10", "--validate", str(BUNDLE)],
                              capture_output=True, text=True, timeout=1800)
        (ROOT / "build" / "pluginval-level10.log").write_text(proc.stdout + proc.stderr, encoding="utf-8")
        record(19, "pluginval strictness 10", proc.returncode == 0, {"exit": proc.returncode})

    passed = sum(t["pass"] for t in report["tests"])
    print(f"\n{passed}/{len(report['tests'])} checks passed")
    (ROOT / "build").mkdir(exist_ok=True)
    (ROOT / "build" / "test-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 0 if passed == len(report["tests"]) else 1


if __name__ == "__main__":
    sys.exit(main())
