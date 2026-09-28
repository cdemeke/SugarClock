"""Synthesises the showreel's 15 s playful soundtrack (120 BPM, C major).

Every hit is placed on the same timeline as showreel.html, so scene
changes, pops, taps and the night-mode dim land on the picture.
Usage: python3 music.py out.wav   (needs numpy)
"""
import sys
import wave

import numpy as np

SR = 44100
DUR = 15.0
N = int(SR * DUR)
L = np.zeros(N)
R = np.zeros(N)
SEND = np.zeros(N)  # reverb send (mono)
rs = np.random.default_rng(42)


def midi(n):
    return 440.0 * 2 ** ((n - 69) / 12)


NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def nm(name):  # "C5" -> midi number
    return 12 * (int(name[-1]) + 1) + NOTE[name[0]] + (1 if "#" in name else 0)


def add(sig, t, gain=1.0, pan=0.0, rev=0.0):
    i = int(t * SR)
    if i >= N:
        return
    sig = sig[: N - i] * gain
    lg, rg = np.sqrt((1 - pan) / 2), np.sqrt((1 + pan) / 2)
    L[i : i + len(sig)] += sig * lg * 1.414
    R[i : i + len(sig)] += sig * rg * 1.414
    SEND[i : i + len(sig)] += sig * rev


def tt(d):
    return np.arange(int(d * SR)) / SR


# ---------------- instruments ----------------
def marimba(f, d=0.6):
    t = tt(d)
    env = np.exp(-t * 9)
    s = np.sin(2 * np.pi * f * t) + 0.35 * np.sin(2 * np.pi * f * 3.98 * t) * np.exp(-t * 30)
    s += 0.15 * np.sin(2 * np.pi * f * 9.9 * t) * np.exp(-t * 60)
    return s * env * np.minimum(1, t * 800)


def glock(f, d=1.2):
    t = tt(d)
    s = np.sin(2 * np.pi * f * t) * np.exp(-t * 3.2)
    s += 0.4 * np.sin(2 * np.pi * f * 2.76 * t) * np.exp(-t * 7)
    s += 0.2 * np.sin(2 * np.pi * f * 5.4 * t) * np.exp(-t * 14)
    return s * np.minimum(1, t * 1500)


def pluck(f, d=0.5, bright=0.5):
    """Karplus-Strong ukulele-ish pluck."""
    n = int(d * SR)
    p = max(2, int(SR / f))
    buf = rs.uniform(-1, 1, p)
    out = np.zeros(n)
    for i in range(0, n, p):
        seg = buf.copy()
        out[i : i + p] = seg[: n - i]
        buf = bright * seg + (1 - bright) * 0.5 * (seg + np.roll(seg, 1))
        buf = 0.5 * (buf + np.roll(buf, 1)) * 0.996
    return out


def lead(f, d):
    """Soft square-ish whistle with vibrato."""
    t = tt(d + 0.08)
    vib = 1 + 0.006 * np.sin(2 * np.pi * 5.5 * t) * np.minimum(1, t * 4)
    ph = 2 * np.pi * np.cumsum(f * vib) / SR
    s = np.sin(ph) + 0.25 * np.sin(3 * ph) + 0.1 * np.sin(5 * ph)
    env = np.minimum(1, t * 60) * np.clip((d + 0.08 - t) / 0.08, 0, 1) * (0.85 + 0.15 * np.exp(-t * 6))
    return s * env


def bass(f, d=0.4):
    t = tt(d)
    ff = f * (1 + 0.5 * np.exp(-t * 60))
    ph = 2 * np.pi * np.cumsum(ff) / SR
    s = np.sin(ph) + 0.3 * np.sin(2 * ph) * np.exp(-t * 10)
    return np.tanh(1.6 * s) * np.exp(-t * 5) * np.minimum(1, t * 400)


def kick(d=0.35):
    t = tt(d)
    f = 45 + 120 * np.exp(-t * 35)
    s = np.sin(2 * np.pi * np.cumsum(f) / SR)
    return s * np.exp(-t * 10) + 0.3 * rs.uniform(-1, 1, len(t)) * np.exp(-t * 200)


def hp(x, a=0.9):
    y = np.empty_like(x)
    prev_x = prev_y = 0.0
    for i, v in enumerate(x):
        prev_y = a * (prev_y + v - prev_x)
        prev_x = v
        y[i] = prev_y
    return y


def noise(d):
    return rs.uniform(-1, 1, int(d * SR))


def clap():
    s = np.zeros(int(0.25 * SR))
    for k, off in enumerate([0, 0.012, 0.024]):
        n = hp(noise(0.2), 0.85) * np.exp(-tt(0.2) * (60 if k < 2 else 22))
        i = int(off * SR)
        s[i : i + len(n)] += n[: len(s) - i]
    return s


def shaker():
    n = hp(noise(0.07), 0.6)
    return n * np.exp(-tt(0.07) * 70) * np.minimum(1, tt(0.07) * 300)


def blip(f, d=0.07):
    t = tt(d)
    return np.sin(2 * np.pi * f * t) * np.exp(-t * 45) * np.minimum(1, t * 3000)


def pop(f0=350, f1=1100, d=0.09):
    t = tt(d)
    f = f0 + (f1 - f0) * (1 - np.exp(-t * 60))
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 35)


def whoosh(d=0.5, rise=True):
    n = noise(d)
    t = tt(d)
    out = np.empty_like(n)
    y = 0.0
    for i, v in enumerate(n):
        k = t[i] / d if rise else 1 - t[i] / d
        a = 0.02 + 0.5 * k * k
        y += a * (v - y)
        out[i] = y
    env = (t / d) ** 2 if rise else (1 - t / d) ** 2
    return hp(out, 0.97) * env


def click():
    t = tt(0.03)
    return (np.sin(2 * np.pi * 2400 * t) + 0.5 * rs.uniform(-1, 1, len(t))) * np.exp(-t * 250)


def crash(d=1.6):
    return hp(noise(d), 0.5) * np.exp(-tt(d) * 3.2) * 0.6


# ---------------- arrangement ----------------
BEAT = 0.5
CHORDS = [  # (start, end, root midi, chord tones)
    (2.0, 4.0, nm("C3"), ["C", "E", "G"]),
    (4.0, 6.0, nm("A2"), ["A", "C", "E"]),
    (6.0, 8.0, nm("F2"), ["F", "A", "C"]),
    (8.0, 10.0, nm("G2"), ["G", "B", "D"]),
    (10.0, 11.5, nm("C3"), ["C", "E", "G"]),
    (11.5, 12.5, nm("F2"), ["F", "A", "C"]),
    (12.5, 13.5, nm("G2"), ["G", "B", "D"]),
]

# Intro: rising glock arpeggio + pixels landing
for i, n in enumerate(["C5", "E5", "G5", "C6", "E6", "G6", "C7"]):
    add(glock(midi(nm(n))), 0.02 + i * 0.125, 0.22, pan=-0.3 + i * 0.1, rev=0.3)
penta = [nm(x) for x in ["C6", "D6", "E6", "G6", "A6", "C7", "D7", "E7"]]
for k in range(22):
    t0 = 0.3 + k * 0.03 + rs.uniform(0, 0.02)
    add(blip(midi(penta[rs.integers(len(penta))]) * 1.0), t0, 0.12, pan=rs.uniform(-0.7, 0.7), rev=0.2)
add(kick(), 1.0, 0.7)
add(pop(200, 700, 0.14), 1.0, 0.5, rev=0.3)
for i, n in enumerate(["C5", "D5", "E5", "G5", "A5", "C6"]):  # letters dropping
    add(marimba(midi(nm(n)), 0.4), 1.15 + i * 0.05, 0.35, pan=-0.4 + i * 0.16, rev=0.25)
add(glock(midi(nm("G6")), 1.4), 1.5, 0.25, rev=0.5)  # tagline sparkle
add(glock(midi(nm("C7")), 1.4), 1.56, 0.18, rev=0.5)

# Transition whooshes into each scene
for s in [2.0, 4.5, 7.0, 9.5, 11.5, 13.5]:
    add(whoosh(0.45), s - 0.45, 0.35, pan=0.0, rev=0.2)
    add(crash(1.2 if s != 13.5 else 2.2), s, 0.18 if s != 13.5 else 0.3, rev=0.3)

# Groove from 2.0 to 13.5
for b in range(int((13.5 - 2.0) / BEAT)):
    t0 = 2.0 + b * BEAT
    night = 12.5 <= t0 < 13.5
    beat_in_bar = b % 4
    if not night or beat_in_bar == 0:
        if beat_in_bar in (0, 2) or (b % 8 == 7):
            add(kick(), t0, 0.75)
    if beat_in_bar in (1, 3) and not night:
        add(clap(), t0, 0.45, pan=0.1, rev=0.25)
    for s16 in range(4):
        if not night or s16 == 2:
            add(shaker(), t0 + s16 * BEAT / 4, 0.16 if s16 % 2 else 0.09, pan=0.45)

for (a, e, root, tones) in CHORDS:
    t0 = a
    step = 0
    while t0 < e - 1e-6:
        # bouncy octave bass on 8ths
        f = midi(root + (12 if step % 2 else 0))
        if step % 4 != 3:
            add(bass(f, 0.3), t0, 0.5)
        # offbeat marimba chord stabs
        if step % 2 == 1:
            for k, tn in enumerate(tones):
                add(marimba(midi(12 * 5 + NOTE[tn]), 0.35), t0 + k * 0.006, 0.13, pan=-0.35 + k * 0.35, rev=0.15)
        # ukulele strum on downbeats
        if step % 4 == 0:
            for k, tn in enumerate(tones + [tones[0]]):
                add(pluck(midi(12 * 4 + NOTE[tn] + (12 if k == 3 else 0)), 0.9, 0.6), t0 + k * 0.012, 0.18, pan=0.35, rev=0.2)
        t0 += BEAT / 2
        step += 1

MELODY = {  # scene-start: [(beat offset, note, beats)]
    2.0: [(0, "E5", .5), (.5, "G5", .5), (1, "C6", .75), (1.75, "B5", .25), (2, "C6", .5), (2.5, "G5", .5), (3, "E5", .5), (3.5, "G5", .5)],
    4.0: [(0, "A5", .5), (.5, "C6", .5), (1, "E6", .75), (1.75, "D6", .25), (2, "C6", .5), (2.5, "A5", .5), (3, "G5", 1)],
    6.0: [(0, "A5", .5), (.5, "F5", .5), (1, "A5", .5), (1.5, "C6", .5), (2, "F6", .75), (2.75, "E6", .25), (3, "D6", .5), (3.5, "C6", .5)],
    8.0: [(0, "B5", .5), (.5, "D6", .5), (1, "G6", 1), (2, "F6", .5), (2.5, "D6", .5), (3, "B5", .5), (3.5, "G5", .5)],
    10.0: [(0, "C6", .5), (.5, "E6", .5), (1, "G6", .5), (1.5, "E6", .5), (2, "C6", .5), (2.5, "G5", .5)],
    11.5: [(0, "A5", .5), (.5, "C6", .5), (1, "F6", 1)],
    12.5: [(0, "G5", .25), (.25, "A5", .25), (.5, "B5", .25), (.75, "D6", .25), (1, "D6", .25), (1.25, "F6", .25), (1.5, "G6", .25), (1.75, "B6", .25)],
}
for start, notes in MELODY.items():
    for off, n, d in notes:
        f = midi(nm(n))
        add(lead(f, d * BEAT * 0.9), start + off * BEAT, 0.16, pan=-0.1, rev=0.35)
        add(marimba(f, 0.5), start + off * BEAT, 0.12, pan=0.2, rev=0.2)

# Scene-synced effects
add(pop(500, 1300), 3.0, 0.3, pan=-0.5)  # chips
add(pop(560, 1400), 3.12, 0.3, pan=-0.3)
add(pop(620, 1500), 3.24, 0.3, pan=-0.1)
add(pop(700, 1700), 3.35, 0.25, pan=0.3)  # "updated" bubble
# range switches: high = up chirp, low = playful buzzer
t = tt(0.18)
add(np.sin(2 * np.pi * np.cumsum(600 + 900 * t / 0.18) / SR) * np.exp(-t * 12), 5.3, 0.25, rev=0.3)
for k in range(3):
    t = tt(0.12)
    add(np.sign(np.sin(2 * np.pi * 880 * t)) * 0.4 * np.exp(-t * 10), 6.1 + k * 0.16, 0.22, pan=-0.2)
add(click(), 6.7, 0.4)  # snooze pill
# companions pop in, ascending
for i, n in enumerate(["C5", "D5", "E5", "G5", "A5", "C6", "D6"]):
    add(pop(midi(nm(n)) * 0.6, midi(nm(n)) * 1.6, 0.1), 7.2 + i * 0.075, 0.3, pan=-0.6 + i * 0.2, rev=0.2)
for i in range(5):  # hearts / greeting sparkle
    add(glock(midi(nm(["E6", "G6", "C7", "E7", "G7"][i])), 0.8), 8.45 + i * 0.06, 0.12, pan=0.4 - i * 0.2, rev=0.5)
# conveyor ticks
for i in range(5):
    add(click(), 9.6 + i * 0.12, 0.15, pan=0.6 - i * 0.3)
# phone: taps and toggles
add(pop(300, 800, 0.08), 11.75, 0.25)  # phone lands
add(click(), 12.45, 0.55, pan=-0.3)
add(marimba(midi(nm("G6")), 0.3), 12.47, 0.2)
add(click(), 12.95, 0.55, pan=-0.3)
add(glock(midi(nm("E7")), 0.6), 12.98, 0.15, rev=0.4)
# outro chord + glock ding + confetti crackle
for k, n in enumerate(["C3", "C4", "E4", "G4", "C5", "E5", "G5"]):
    add(pluck(midi(nm(n)), 1.5, 0.7), 13.5 + k * 0.015, 0.2, pan=-0.3 + k * 0.1, rev=0.4)
    add(marimba(midi(nm(n)), 0.9), 13.5, 0.1, rev=0.3)
add(bass(midi(nm("C2")), 1.2), 13.5, 0.7)
add(kick(0.6), 13.5, 0.9)
for i, n in enumerate(["C6", "E6", "G6", "C7"]):
    add(glock(midi(nm(n)), 1.4), 13.5 + 0.18 + i * 0.09, 0.2, pan=-0.3 + i * 0.2, rev=0.6)
for k in range(30):
    add(blip(rs.uniform(2500, 6000), 0.03), 13.55 + rs.uniform(0, 1.0), 0.05, pan=rs.uniform(-1, 1))
add(lead(midi(nm("C6")), 0.9), 14.0, 0.14, rev=0.5)
add(lead(midi(nm("E6")), 0.25), 13.75, 0.12, rev=0.5)
add(lead(midi(nm("G6")), 0.25), 13.87, 0.12, rev=0.5)

# ---------------- mix ----------------
# reverb: exponentially decaying noise impulse response
ir_t = tt(1.3)
ir = rs.normal(0, 1, len(ir_t)) * np.exp(-ir_t * 4.5)
ir[: int(0.012 * SR)] = 0
wet = np.fft.irfft(np.fft.rfft(SEND, 2 * N) * np.fft.rfft(ir, 2 * N))[:N]
wet *= 0.12 / (np.abs(wet).max() + 1e-9) * np.abs(SEND).max()
L += wet
R += np.roll(wet, 300)

# night-mode dim: low-pass the mix 12.5–13.5, snap back open on the outro
for ch in (L, R):
    a0, a1 = int(12.4 * SR), int(13.5 * SR)
    y = ch[a0 - 1]
    for i in range(a0, a1):
        k = (i - a0) / (a1 - a0)
        a = 1 - 0.93 * np.sin(np.pi * min(1, k * 1.6) / 2) if k < 0.95 else 1 - 0.93 * (1 - (k - 0.95) / 0.05)
        y += a * (ch[i] - y)
        ch[i] = y

mix = np.stack([L, R], 1)
mix /= np.abs(mix).max()
mix = np.tanh(mix * 1.6) / np.tanh(1.6)
fade = np.ones(N)
fade[-int(0.35 * SR) :] = np.linspace(1, 0, int(0.35 * SR)) ** 2
fade[: int(0.005 * SR)] = np.linspace(0, 1, int(0.005 * SR))
mix *= fade[:, None] * 0.9
pcm = (mix * 32767).astype(np.int16)
with wave.open(sys.argv[1] if len(sys.argv) > 1 else "music.wav", "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(pcm.tobytes())
print("wrote", len(pcm) / SR, "s")
