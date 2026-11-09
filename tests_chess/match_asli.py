#!/usr/bin/env python3
"""Adu Ren engine vs engine UCI lain (mis. stockfish) di Termux.
Pakai:  python match.py [jumlah_partai] [movetime_ms] [stockfish_skill 0-20]
"""
import subprocess, sys, chess

GAMES = int(sys.argv[1]) if len(sys.argv) > 1 else 10
MOVETIME = int(sys.argv[2]) if len(sys.argv) > 2 else 200
SKILL = int(sys.argv[3]) if len(sys.argv) > 3 else 3

class Uci:
    def __init__(self, cmd, opts=None):
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, bufsize=1)
        self.send("uci"); self.wait("uciok")
        for k, v in (opts or {}).items():
            self.send(f"setoption name {k} value {v}")
        self.send("isready"); self.wait("readyok")
    def send(self, s):
        self.p.stdin.write(s + "\n"); self.p.stdin.flush()
    def wait(self, tok):
        while True:
            l = self.p.stdout.readline()
            if not l: raise RuntimeError("engine mati")
            if l.startswith(tok): return l.strip()
    def move(self, moves, ms):
        self.send("position startpos" + (" moves " + " ".join(moves) if moves else ""))
        self.send(f"go movetime {ms}")
        return self.wait("bestmove").split()[1]
    def new(self): self.send("ucinewgame"); self.send("isready"); self.wait("readyok")
    def close(self): self.send("quit")

ren = Uci(["./ren_uci"], {"Depth": 8})
opp = Uci(["stockfish"], {"Skill Level": SKILL})   # ganti ini kalau mau lawan lain
score = {"ren": 0.0, "opp": 0.0}

for g in range(GAMES):
    ren.new(); opp.new()
    b = chess.Board(); moves = []
    ren_white = (g % 2 == 0)                      # gantian warna
    while not b.is_game_over(claim_draw=True) and len(moves) < 300:
        ren_turn = (b.turn == chess.WHITE) == ren_white
        mv = (ren if ren_turn else opp).move(moves, MOVETIME)
        try: b.push_uci(mv)
        except Exception: print("langkah ilegal:", mv, "oleh", "ren" if ren_turn else "opp"); break
        moves.append(mv)
    r = b.result(claim_draw=True)
    if r == "1-0": w = "ren" if ren_white else "opp"
    elif r == "0-1": w = "opp" if ren_white else "ren"
    else: w = None
    if w: score[w] += 1
    else: score["ren"] += .5; score["opp"] += .5
    print(f"partai {g+1}: {r} {len(moves)//2}lgk ({'Ren putih' if ren_white else 'Ren hitam'}) | Ren {score['ren']} - {score['opp']} Lawan")

ren.close(); opp.close()
