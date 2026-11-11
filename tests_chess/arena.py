#!/usr/bin/env python3
"""Adu-main dua konfigurasi ren_uci (tanpa dependensi). Pakai:
  python3 arena.py BIN_A EV2_A BIN_B EV2_B [partai=40] [movetime_ms=50] [seed=1]
Partai dimulai dari pembukaan acak 6 langkah; tiap pembukaan dimainkan 2x (warna ditukar).
Keluaran terakhir: skor B (kandidat) terhadap A, plus perkiraan selisih Elo dan batas kepercayaan kasar."""
import subprocess, sys, random, math

class Uci:
    def __init__(self, exe, ev2):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.send("uci"); self.wait("uciok")
        for k, v in (("Depth", 14), ("Mode", 0), ("Margin", 0), ("Ev2", ev2)): self.send(f"setoption name {k} value {v}")
        self.send("isready"); self.wait("readyok")
    def send(self, s): self.p.stdin.write(s + "\n"); self.p.stdin.flush()
    def wait(self, tok):
        while True:
            l = self.p.stdout.readline()
            if not l: raise RuntimeError("engine mati")
            if l.startswith(tok): return l.strip()
    def pos(self, moves): self.send("position startpos" + (" moves " + " ".join(moves) if moves else ""))
    def move(self, moves, ms): self.pos(moves); self.send(f"go movetime {ms}"); return self.wait("bestmove").split()[1]
    def status(self, moves): self.pos(moves); self.send("status"); return int(self.wait("status").split()[1])
    def legal(self, moves): self.pos(moves); self.send("legal"); return self.wait("legal").split()[1:]
    def new(self): self.send("ucinewgame"); self.send("isready"); self.wait("readyok")

binA, evA, binB, evB = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4])
N = int(sys.argv[5]) if len(sys.argv) > 5 else 40
MS = int(sys.argv[6]) if len(sys.argv) > 6 else 50
rnd = random.Random(int(sys.argv[7]) if len(sys.argv) > 7 else 1)
A, B, R = Uci(binA, evA), Uci(binB, evB), Uci(binB, 0)       # R = wasit
def opening():
    while True:
        mv = []
        for _ in range(6):
            l = R.legal(mv)
            if not l: break
            mv.append(rnd.choice(l))
        if len(mv) == 6 and R.status(mv) == 0: return mv

wins = draws = losses = 0
games = 0
while games < N:
    op = opening()
    for flip in (0, 1):
        if games >= N: break
        A.new(); B.new(); R.new()
        mv = list(op); b_white = (flip == 0)
        res = None
        while True:
            st = R.status(mv)
            if st == 1: res = "B" if ((len(mv) % 2 == 1) == b_white) else "A"; break   # yg jalan kalah
            if st != 0: res = "D"; break
            if len(mv) >= 240: res = "D"; break
            white_to_move = len(mv) % 2 == 0
            b_turn = (white_to_move == b_white)
            m = (B if b_turn else A).move(mv, MS)
            if m == "0000": res = "D"; break
            mv.append(m)
        games += 1
        if res == "B": wins += 1
        elif res == "A": losses += 1
        else: draws += 1
        print(f"partai {games}: {res} ({len(mv)} ply) | B {wins}-{draws}-{losses} A", flush=True)
sc = (wins + 0.5 * draws) / games
sc = min(max(sc, 0.01), 0.99)
elo = -400 * math.log10(1 / sc - 1)
var = (wins * (1 - sc) ** 2 + draws * (0.5 - sc) ** 2 + losses * (0 - sc) ** 2) / games
se = math.sqrt(var / games); lo, hi = sc - 1.96 * se, sc + 1.96 * se
f = lambda x: -400 * math.log10(1 / min(max(x, 0.01), 0.99) - 1)
print(f"HASIL B vs A: +{wins} ={draws} -{losses}  skor {sc:.3f}  Elo {elo:+.0f}  (95%: {f(lo):+.0f} .. {f(hi):+.0f})")
