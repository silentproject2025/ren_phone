# Chess (online and vs. AI) `BETA`

Complete chess (check, checkmate, stalemate, castling, *en passant*, promotion, the 50-move rule, threefold repetition) with two ways to play. The board rotates automatically for Black, and the panel under the board can be hidden. The source is `firmware/phone/chess_renphone.ino` (plus `extras/chess_smp.cpp` for the second core).

> **Status.** Everything below was tested **on a PC** (rules and *perft*, the strength of each level, training and serialisation of the style model). Real behaviour on two devices and against a real broker is unconfirmed, apart from the on-device measurements noted in [NYX-MAX](#nyx-max-chess-strength-up-to-the-hardware-limit) and [Two cores and the NPS meter](#two-cores-and-the-nps-meter-nyx-lazy-smp-experimental).

- [Modes](#modes)
- [Broker](#broker)
- [How online play works](#how-online-play-works)
- [AI levels](#ai-levels)
- [Search engine](#search-engine-used-by-all-levels)
- [Nyx (experimental level)](#nyx-experimental-level)
- [Mirror and AFK](#mirror-and-afk)
- [NYX-MAX](#nyx-max-chess-strength-up-to-the-hardware-limit)
- [Evaluation v2 (inactive)](#evaluation-v2-inactive)
- [Network screen](#network-screen-looking-inside-the-ais-brain)
- [Engine vs. Gemini](#engine-vs-gemini-testing-ai-strength)
- [Two cores and the NPS meter](#two-cores-and-the-nps-meter-nyx-lazy-smp-experimental)
- [Threefold repetition](#threefold-repetition)
- [Limitations](#limitations)

## Modes

| Mode | How it works |
|---|---|
| **Online** | Two phones open the **Chess** app, enter the same **room code** (at least 3 characters) and press **Start**. The phone that joins first plays White; the second plays Black. **Play again** and **Give up** (with confirmation) are available. Requires WiFi and an MQTT broker. |
| **vs. AI** | No network at all. The AI-level button cycles through Easy, Medium, Hard, Mirror and Nyx. |

## Broker

Chess uses **its own** broker settings, separate from Chat (TLS, default port 8883; `host` may carry `:port`). Fill them in through the app (host, username, password, then **Save**), through a `/chess.cfg` file on the SD card (`host=`, `user=`, `pass=`; see [Configuration](../configuration.md)), or with the copy button from the Chat settings (after copying, the two remain separate). If the broker account is restricted by ACL to `renphone/chat/#`, add *publish* and *subscribe* permission to `renphone/chess/#`.

## How online play works

- Each game is stored as a *retained* message containing the **entire move list** (coordinate notation, for example `e2e4 e7e5 g1f3`) together with a generation number and the result. Because the full list is always sent, duplicate, late or interrupted messages do not corrupt the game, and an opponent who connects later immediately gets the latest position.
- The White and Black seats are also *retained* messages; player presence uses a *Last Will*, so a phone that drops is marked offline.
- Like Chat, the connection only exists while the Chess app is open. If internal RAM is too tight, the app shows a message asking you to close other apps.

## AI levels

| Level | Behaviour |
|---|---|
| **Easy** | Shallow search (depth 2); picks randomly among moves that are still reasonable, with a wide tolerance. |
| **Medium** | Depth 4; picks randomly among reasonable moves with a narrow tolerance. |
| **Hard** | Depth up to 12 within a 4-second limit (NYX-MAX; formerly depth 6); always plays the best move according to its search. |
| **Mirror** | Depth 4; among moves whose value is still close to the best, the AI picks the move most **similar to the owner's style**. |
| **Nyx** `experimental` | Depth up to 14 within a 7-second limit (NYX-MAX; formerly 2.4 s), tolerance 15 centipawns (previously depth 4, 0.9 s, tolerance 80). Predicts **your reply** with an opponent model that learns on the device (Mirror style + tactical gain + greed), then picks the move that most punishes your habits, **only when the model has proven it guesses better than random**. See [below](#nyx-experimental-level). |

## Search engine (used by all levels)

The *negamax* search now uses PVS with a 64-bit transposition table (2^16 entries, ~768 KB in PSRAM; if allocation fails, search still runs without the table; only entries from the currently running search are trusted, because old scores depend on history and make the AI think winning positions are draws), *null-move*, *late move reduction*, *futility* and *reverse futility* pruning, move ordering based on the TT move, MVV-LVA, *killer* and *history* heuristics, and repetition detection along the search path. *Quiescence* uses *delta pruning*. The evaluation adds pawn structure (doubled, isolated, passed), the bishop pair, rooks on open files or the 7th rank, knight/bishop/rook mobility, the king's pawn shield, and endgame *mop-up* (pushing the opponent's king to the edge when ahead) so that K+R vs K or K+Q vs K are really finished off.

Because the recursion runs in the task loop (8 KB stack), the depth is limited to `CHS_AI_MAXD = 14` and `CHS_AI_PLY = 32`, plus a runtime stack guard (`CHS_AI_STACK_MAX`, 4,800 bytes). The NVS format (`sty`, `hab`, `bk`, `rb`, `mix`) is **unchanged**; the TT hash differs from the `chsPosHash` used by the opening book.

**PC results (not on the device).**

- Final Nyx (depth 14, tolerance 15) beat the old repository Nyx (depth 4, tolerance 80) 20–0 with ESP32-scaled timing; the final parameters beat the previous version's parameters (depth 10, tolerance 30) 13–6–5 on the same engine, so the last increase is real but moderate.
- Endgame conversion was tested on 9 K+R/K+Q vs K positions (all checkmates) and 50 random K+R vs K positions (50/50; the old engine 47/50).
- On the same `bench_nyx2.cpp` benchmark as before (48 games, 60 plies), against a *reasonable habit-driven* bot the old Nyx delivered checkmate 26/48 and the new Nyx 33/48; the spread between runs is still a few games of noise.
- These numbers use the engine on a PC; **actual speed on the ESP32-S3 was not measured at that time**, so the depth reached in 2.4 seconds on the device will be lower than on a PC. The difference caused by the habit-trap logic (the opponent model) itself is still not proven separately from the increase in base engine strength.

## Nyx (experimental level)

The fifth level adds home-grown logic on top of the existing engine. The minimax search still filters safe candidates (within 80 centipawns of the best, at most 8 candidates). For each candidate, the AI plays the move and then estimates the **probability of each of your legal replies** with a *product of experts* model: probability ∝ exp(θ₁·style score + θ₂·tactical gain + θ₃·greed). The *style score* is the network and habit table of Mirror, the *tactical gain* is the value of the capture search (*quiescence*) after that move, and the *greed* is the value of the captured piece.

The three θ weights are trained directly on the device every time you move, using a **recursive Newton step** (conditional logit regression; precision accumulates with a forgetting factor of 0.97 so a changing style is tracked). The value of each candidate uses an **entropic soft-min** `F = −(1/k)·ln Σ pᵣ·exp(−k·Vᵣ)` with `k = 0.01` per centipawn. `F` always lies between `M` (you reply best) and `E` (the average according to the model), so a rare but painful reply is still counted and the AI is not too optimistic. The difference `F − M` is multiplied by the model's confidence (`1 − H/ln n`, `H` = entropy of the reply probabilities), clamped to 150, then added to the candidate's score with weight `0.9 · g`.

The **gate `g`** is `min(1, N/30) · min(1, IG/1.2)`, where `IG` is the moving average (12 moves) of `ln(probability the model gave to your move) − ln(1/n)`, computed *before* the model learns from that move (prequential, so it is honest). If the model does not guess better than random, `g = 0` and Nyx plays as pure minimax; the trap weight only grows after the model has proven it can guess your moves.

The mixture model (θ, precision, `IG`) is stored in NVS under the key `mix` (44 bytes) and is part of Backup & Restore; a missing key (an older device) means initial values, so the `sty`, `hab`, `bk` and `rb` formats are unchanged. The model is only observed when the Nyx level is selected, because it costs n capture searches per move of yours; observation is limited to 100 ms and skipped if exceeded, so the gate stays closed and Nyx plays like minimax (fail-safe).

**Status.** The logic and its safety are tested on a PC (`tests/chess/test_chess.cpp`: moves are always legal, the board is unchanged, mate-in-1 is still played, a zero bonus on a flat model, a corrupted `mix` blob is rejected, the gate opens for a predictable player and stays closed for a random one).

PC experiments (`tests/chess/bench_nyx2.cpp`, 48 games per cell, 60 plies, new seed) against a *reasonable habit-driven* bot (2-ply search, 70% capturing the largest reasonable piece):

| Engine | Checkmates |
|---|---|
| Pure minimax | 30/48 |
| **First Nyx version** | **16/48** (clearly harmful) |
| This Nyx version | 31/48 |

Against a bot that 95% always captures the largest piece: 45/48, 44/48 and 46/48.

**The advantage over pure minimax is not proven**: the difference is still within noise, and the mixture model only adds small information (0.03 to 0.12 nats per move) on top of the style model alone. What *is* proven is that the regression of the first version is gone. Not tested on a device; the observation cost on a PC averages about 2 ms per move of yours (maximum 38 ms) and has not been measured on the ESP32-S3. This level is therefore still marked experimental.

## Mirror and AFK

The AI engine uses *negamax* alpha-beta search with *quiescence* and an evaluation of material and piece-square tables. The Mirror level adds three small components that **learn directly on the ESP32** each time the owner moves:

- a mini neural network (26 inputs, 16 hidden neurons, 1 output; 449 parameters) that scores how similar a move is to the owner's choices;
- a **personal opening book** (positions and the moves most often played);
- a **square habit table** (for example always opening with `e4`, or liking to advance a knight to `f3`).

The model is stored in NVS so it is not lost when the app closes. In online games, if a player stays idle past the **AFK** limit (30, 60 or 120 seconds; can be turned off), the owner-style AI plays their turn so the opponent does not have to wait. Because the AI only chooses among moves that are still close to the best, the game is not simply thrown away.

## NYX-MAX: chess strength up to the hardware limit

Three levers were raised at once:

- **-O3 compilation** for the ENGINE + AI block (formerly -O2; the rest of the sketch stays -Os). The workflow input `chess_opt` = `O3` (default) or `O2` (smaller code, for A/B tests or if the 3 MB app partition is full). Flag: `-DCHS_OPT3=0`.
- **Maximum CPU cache** (custom SDK library, *Build Arduino Libs* workflow): 64 KB data cache and 32 KB instruction cache, both 8-way. The 64 B cache line is deliberately **off** (option `psram_cache_line_64b`): DMA buffers from PSRAM must be 64 B-aligned and were suspected of causing a crash at boot. This attacks the bottleneck noted above: two cores contend for the cache while reading the transposition table in PSRAM. The price is **~48 KB of internal RAM** (64 KB dcache = −32 KB, 32 KB icache = −16 KB); if "RAM tight" messages appear more often, re-run that workflow with `dcache_kb=32` and/or `icache_kb=16`. The custom libraries must be rebuilt once for this to take effect. The first build is confirmed in CI: `DATA_CACHE_64KB`, `INSTRUCTION_CACHE_32KB` and both 8-way settings all end up in the resulting `sdkconfig` (its effect on on-device speed had not yet been measured at that point; see the measurement below).
- **Thinking time**: Hard 1.8 → 4 seconds, Nyx 2.4 → 7 seconds, the pondering limit 5 → 10 seconds per turn of yours. Easy, Medium and Mirror deliberately stay fast.

What was deliberately **not** changed: the CPU is already 240 MHz; OPI PSRAM is already 80 MHz (120 MHz is experimental and unstable on many boards); the transposition table stays at 1.5 MB because at ~30 thousand nodes/second a 7-second search only visits ~200 thousand nodes, so a larger table adds nothing and only uses PSRAM.

**On-device measurement (NPS tab, *NPS test*, 3 fixed positions × 1 second).** Before NYX-MAX: 19.2k nodes/second (1 core) and 29.9k (2 cores, ×1.55). After: **22.7k** (1 core, about +18%) and **38.1k** (2 cores, about +27%), a two-core speed-up of **×1.68**. A single test run; numbers shift by a few percent between runs, so read them as a direction, not an exact figure. The real depth reached by Hard and Nyx with the new timings has not been measured.

**Safety note (a lesson from testing).** If, after changing the custom SDK library, the device **restarts by itself right after the boot screen**, return the cache options to known-safe values: `psram_cache_line_64b` off, `dcache_kb=32`, `icache_kb=16` (the same as the IDF default), then raise them one at a time. The 64 B cache line is the prime suspect because it demands PSRAM DMA buffers aligned to 64 B. The default configuration documented here (64 B line off, data 64 KB, instruction 32 KB) was tried and boots normally on the device.

## Evaluation v2 (inactive)

`chess_renphone.ino` contains a `CHS_EV2_DEF` switch (a bitmask, default 0) for extra evaluation terms: 1 = tapered, 2 = queen mobility, 4 = knight outpost, 8 = protected pawns, 16 = king distance to passed pawns. In the PC head-to-head `tests/chess/arena.py` (40 ms per move, 100–160 games) none was proven to win: all combined −28 Elo, tapered +60 over 100 games then below 50% on a replay, outpost +24, king distance +17, queen mobility −35, protected pawns −35, tapered+outpost −4 (wide confidence interval, ±60 Elo). It is therefore left off; the AI behaves as before.

## Network screen (looking inside the AI's brain)

On the Chess setup screen, under the *vs. AI | level | AFK* row, there is a small **Network** chip on the right. This screen shows the **trained weights** of the style network directly on the phone; what is shown is the result of learning, not a loss chart or a log of each training step. The Back button closes it. In all views, the theme accent color means a positive weight and red a negative one; the denser the color, the larger relative to the largest value currently shown.

| Tab | Content and how to use it |
|---|---|
| **Network** | A 26-16-1 diagram: 26 input dots on the left (gray for the 20 move features, other colors for the 6 position contexts), 16 hidden neurons in the middle and 1 score dot on the right. Only strong input→neuron lines (above 35% of the largest weight) are drawn so the screen does not fill up. The fill color of each neuron shows its weight toward the score (accent = pushes the score up, red = lowers it). **Tap a neuron** (a middle circle): the other neurons' lines are dimmed, all of that neuron's lines are shown, and an info row shows the neuron's score and the two features that influence it most, for example `H7 score +0.31 \| capture +0.42, check -0.28`. Tap again to cancel. |
| **Weights** | A heat map of the first-layer weight matrix: 16 rows (H1 to H16) by 26 columns (features 1 to 26), so all 416 weights are visible at once. **Tap a cell** for its value, for example `H3 x capture: +0.215`. |
| **Squares** | The square habit table as 7 8×8 boards: one board for each destination piece type (Pawn, Knight, Bishop, Rook, Queen, King) and one **Origin** board for squares that are left; White is at the bottom. Dense squares are those you often move to or leave. **Tap a square** for its value, for example `Knight f3: +1.80`. |
| **Diff** (chip) | Switches the view from the current weights to the **difference from the initial weights**, so only the weights that changed because of learning from your moves are colored. The initial weights are recomputed from the same random generator as the initialisation and kept in PSRAM only while this screen is open. If RAM is not enough, the message *RAM not enough for diff* appears. |

Two text lines under the view show the latest touch info (neuron, cell or square) and the training status, for example `54 moves | guess 62% | weights`. **Moves** is the number of your moves learned from. **Guess** is the moving average of the last 20 moves: how often the network's top choice, before learning from that move, equals the move you played. It only appears after at least 5 moves; a persistently low guess means the network has not captured a pattern or your style simply varies.

How to use it: play a few games against the AI, open **Weights** with **Diff** on to see which features the AI has learned from your style, then open **Squares** to see your opening-square habits (for example `e4` and `f3` lit up).

## Engine vs. Gemini (testing AI strength)

On the Chess setup screen, next to the **Network** chip, there is a **Gemini** chip. The local engine (the level chosen in the *vs. AI | level | AFK* row) plays against Gemini through the same API key as AI Chat (`/gemini_key.txt`), and you watch. Gemini is given the FEN, the move history and the **list of legal moves**, and is asked to reply with a single UCI move. An illegal reply is repeated (at most 3 times per move); three illegal replies in a row means Gemini loses. If the network or API fails, the game is paused with an error message and can be resumed with **Continue**. The panel's left button is **Pause/Continue**, then **Play again** (colors swapped) after a game ends. A score per level (`M` engine wins, `G` Gemini wins, `S` draw) is stored in NVS and shown in the panel. Requires WiFi. The default model is the same as AI Chat; for a different opponent, set `CHS_GEM_MODEL` in `chess_renphone.ino`.

Turn logic, reply parsing and score counting are tested on a PC with a mock Gemini (`tests/chess/run_gem.sh`); real API calls and the screen display have not been tested on a device.

## Two cores and the NPS meter (Nyx, Lazy SMP) `experimental`

Nyx search can use both ESP32-S3 cores. The core other than `loop()`'s runs a **copy of the engine of its own** (`extras/chess_smp.cpp`, built as a separate file; CI copies the ENGINE+AI block from `chess_renphone.ino` to `build_sketch/src/chess_engine.inc`), so the board, killers and history are separate. Only the transposition table (TT) in PSRAM is shared, without a mutex: entries are read/written as three 32-bit words with an XOR-ed key, so an entry that is "torn" because two cores write at once is detected and treated as a miss. The helper core searches one depth ahead of the main iteration and fills the TT; the main search uses it to prune earlier.

- Active only when the time budget is ≥ 400 ms and the maximum depth is ≥ 5 (so not for pondering, Easy and Medium) and the main NN is not active. The `nyxHelper` task (10 KB stack in PSRAM, priority 1, the core opposite `loop()`) is created once and then parked, and yields the CPU for 1 tick about every 25 ms.
- **Viewing NPS:** the Serial Monitor prints `[NYX-NPS] d=… node=main+helper … ms NPS=… core=N` every time the AI finishes searching. The **Network** screen has an **NPS** tab: the last search, an **NPS test** button (3 fixed positions × 1 second, once with 1 core and once with 2 cores, about 6 seconds; only when no game is running) and a **2nd core** switch (stored in NVS under the key `smp`). The test result is also printed as `[NYX-BENCH]`.
- Turn it off at build time with `-DCHS_SMP=0` (exactly one core as before).
- **On-device measurement:** 19.2k → 29.9k nodes/second (×1.55), but the average depth within 1 second went 6.0 → 5.7. So raw speed increased, but **strength is not proven to have increased**: the two cores contend for PSRAM and cache, and the main core slows down. A fairer test (time to reach a given depth) does not exist yet. On a PC, `tests/chess/run_smp.sh` tests correctness (moves always legal, mate and tactics still correct, repeated start/stop without deadlock), not speed.

## Threefold repetition

When the same position (board, side to move, castling rights, *en passant*) occurs three times, the game ends in a draw ("Draw: threefold repetition"). It is computed from the game's move list, so it is the same on both devices in online mode. Tested on a PC (`ren_uci`: the `status` command).

## Limitations

- There is no chess clock.
- Online mode needs an MQTT broker with your own account (separate from Chat).
- Real behaviour on two devices and with a real broker is unconfirmed.
