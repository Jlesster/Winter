#!/usr/bin/env python3
"""Differential test: feed the same bytes to tmux (as terminal emulator) and to our Vt."""
import os, random, subprocess, sys, time, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
SOCK = "vtdiff"
ROWS, COLS = 12, 40
TMUX = ["tmux", "-L", SOCK, "-f", "/dev/null"]

def sh(*a, **k):
    return subprocess.run(a, capture_output=True, text=True, **k)

def tmux(*a):
    return subprocess.run(TMUX + list(a), capture_output=True, text=True)

def tmux_render(payload: bytes, tag: str):
    pf = os.path.join(HERE, f"p_{tag}.bin")
    done = os.path.join(HERE, f"d_{tag}")
    open(pf, "wb").write(payload)
    if os.path.exists(done): os.remove(done)
    cmd = f"stty -opost -echo; cat {pf}; touch {done}; sleep 30"
    r = tmux("new-session", "-d", "-s", tag, "-x", str(COLS), "-y", str(ROWS), "sh", "-c", cmd)
    if r.returncode: raise RuntimeError(r.stderr)
    for _ in range(400):
        if os.path.exists(done): break
        time.sleep(0.01)
    time.sleep(0.03)
    cap = tmux("capture-pane", "-p", "-t", tag).stdout
    cur = tmux("display-message", "-p", "-t", tag, "#{cursor_x} #{cursor_y}").stdout.split()
    tmux("kill-session", "-t", tag)
    os.remove(pf); 
    if os.path.exists(done): os.remove(done)
    rows = cap.split("\n")
    rows = rows[:ROWS] + [""] * (ROWS - len(rows))
    return [r.rstrip() for r in rows[:ROWS]], (int(cur[0]), int(cur[1]))

def ours(payload: bytes):
    out = subprocess.run([os.path.join(HERE, "vt_test"), "dump", str(ROWS), str(COLS)],
                         input=payload, capture_output=True).stdout.decode()
    lines = out.split("\n")
    cur = lines[ROWS].split()
    return [l.rstrip() for l in lines[:ROWS]], (int(cur[1]), int(cur[2]))

WORDS = ["abc", "hello", "x", "wxyz", "0123456789", "the quick brown fox jumps over", "é", "中文", "ab中cd"]
def tok(rng):
    k = rng.random()
    n = lambda hi=15: rng.randint(1, hi)
    if k < .22: return rng.choice(WORDS).encode()
    if k < .30: return b"\r\n"
    if k < .33: return b"\n"
    if k < .35: return b"\r"
    if k < .37: return b"\x08"
    if k < .40: return b"\t"
    if k < .48: return f"\x1b[{n(14)};{n(44)}H".encode()
    if k < .52: return f"\x1b[{n()}{rng.choice('ABCD')}".encode()
    if k < .54: return f"\x1b[{n(44)}G".encode()
    if k < .56: return f"\x1b[{n(14)}d".encode()
    if k < .60: return f"\x1b[{rng.choice([0,1,2])}J".encode()
    if k < .66: return f"\x1b[{rng.choice([0,1,2])}K".encode()
    if k < .70: return f"\x1b[{n(4)}{rng.choice('LM')}".encode()
    if k < .75: return f"\x1b[{n(6)}{rng.choice('@PX')}".encode()
    if k < .78:
        a = n(8); b = rng.randint(a + 1, 12)
        return f"\x1b[{a};{b}r".encode()
    if k < .79: return b"\x1b[r"
    if k < .83: return rng.choice([b"\x1bD", b"\x1bE", b"\x1bM"])
    if k < .85: return f"\x1b[{n(3)}{rng.choice('ST')}".encode()
    if k < .87: return rng.choice([b"\x1b7", b"\x1b8"])
    if k < .89: return rng.choice([b"\x1b[?7l", b"\x1b[?7h"])
    if k < .91: return rng.choice([b"\x1b[?1049h", b"\x1b[?1049l"])
    if k < .93: return f"\x1b[{rng.choice([0,1,31,42,'38;5;99','1;4'])}m".encode()
    return rng.choice(WORDS).encode()

def gen(seed, n=60):
    rng = random.Random(seed)
    toks = [tok(rng) for _ in range(n)]
    # tmux runs IL/DL on the whole screen when the cursor is outside the scroll
    # region; xterm ignores them. Don't compare that corner.
    if any(t.endswith(b"r") and t.startswith(b"\x1b[") and t != b"\x1b[r" for t in toks):
        toks = [t for t in toks if not (t.startswith(b"\x1b[") and t[-1:] in (b"L", b"M"))]
    return toks

def run_case(toks, tag):
    p = b"".join(toks)
    a = tmux_render(p, tag)
    b = ours(p)
    return a, b

def minimize(toks, tag):
    cur = list(toks)
    chunk = max(len(cur) // 2, 1)
    while chunk >= 1:
        i = 0
        progressed = False
        while i < len(cur):
            trial = cur[:i] + cur[i + chunk:]
            if trial:
                a, b = run_case(trial, tag)
                if a != b:
                    cur = trial; progressed = True; continue
            i += chunk
        if not progressed: chunk //= 2
    return cur

def show(toks, a, b):
    print("  payload:", b"".join(toks))
    for r in range(ROWS):
        if a[0][r] != b[0][r]:
            print(f"  row {r:2d} tmux=[{a[0][r]}]\n         ours=[{b[0][r]}]")
    if a[1] != b[1]: print(f"  cursor tmux={a[1]} ours={b[1]}")

if __name__ == "__main__":
    start, count = int(sys.argv[1]), int(sys.argv[2])
    nmin = int(sys.argv[3]) if len(sys.argv) > 3 else 6
    tmux("kill-server")
    bad = 0; shown = 0
    for seed in range(start, start + count):
        toks = gen(seed)
        a, b = run_case(toks, f"s{seed}")
        if a != b:
            bad += 1
            if shown < nmin:
                shown += 1
                m = minimize(toks, f"m{seed}")
                a, b = run_case(m, f"f{seed}")
                print(f"MISMATCH seed {seed} (minimised {len(toks)} -> {len(m)} tokens)")
                show(m, a, b)
    print(f"{count} cases, {bad} mismatches")
    tmux("kill-server")
