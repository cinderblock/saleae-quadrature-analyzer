# Quadrature encoder analyzer for Logic 2

## Goal

A new Logic 2 low-level analyzer (C++ AnalyzerSDK) that decodes an incremental quadrature encoder live in Logic. It takes the A and B channels, plus an optional Z/index channel, and reports the count/position, direction, speed, index pulses and invalid transitions.

The user asked for a brand-new, separate analyzer (2026-10-07). This is "option B" from `../plans/logic2-derived-signals.md`: write a new analyzer rather than port the 2011 dirkx/ktgilliam one.

## Environment / context

- **Repo:** `C:\Users\camer\git\Personal Projects\Saleae Logic Plugins\quadrature-analyzer`. Its own git repo on branch `master`, no remote yet.
- **Scaffold:** taken from `../stepper-analyzer`, which itself came from saleae/SampleAnalyzer. That includes `build.ps1`, the CI workflow (with the Windows DLL-copy fix), and the isolated Logic test-instance and install scripts.
- **Toolchain:** VS 2022, CMake 3.30, Ninja, clang-format 15. WSL Ubuntu with g++ 13 for Linux checks; download CMake and Ninja into `/tmp` there.
- **Logic:** 2.4.46.
  - Everyday instance: automation on 10430. The stepper analyzer is installed there.
  - Isolated test instance: `tools/logic-test-instance.ps1` on port 10431. Unset `ELECTRON_RUN_AS_NODE` before launching Logic.
- **Real test data** (`testdata/`, gitignored because these are forum users' files):
  - Both come from https://discuss.saleae.com/t/analyser-for-quadrature-encoder/2556.
  - `TU_Linear_encoder1.sal`: Logic Pro 16 at 250 MS/s, ENCA = ch0, ENCB = ch1, ch2 = trigger.
  - `Session01_6moves.sal`: same channels, ch2 = "Steppos"; six moves in both directions.

## Decisions already made (don't re-ask)

- New, separate analyzer and repo. Not a port of the existing one.
- Live in Logic only. No offline `.sal` processing; see memory `saleae-plugins-must-be-live`.
- Same result conventions as the stepper analyzer:
  - each frame is an interval of constant count;
  - a *Moves and holds* mode for readable zoomed-out views;
  - FrameV2 fields for the data table.
- Publishing to GitHub and installing into everyday Logic need the user's go-ahead for *this* repo. The stepper approvals don't carry over.

## Design

### Decoding

States are Gray-coded (A, B):

| State | (A, B) |
| --- | --- |
| 0 | 00 |
| 1 | 10 |
| 2 | 11 |
| 3 | 01 |

- **Forward** (A leads B): 0 → 1 → 2 → 3 → 0. Step = (new − old) mod 4; 1 means +1 and 3 means −1.
- **Invalid transition:** both channels change on the same sample. That is a skipped state with unknown direction. It produces an error frame and an error marker, and the count stays put.

| Mode | Counts on | Relation to the X4 count |
| --- | --- | --- |
| X4 | every transition | — |
| X2 | A edges only | x2 = floor((x4 + 1) / 2) |
| X1 | A edges whose new B is 0 (A↑ with B=0 → +1, A↓ with B=0 → −1) | x1 = floor((x4 + 3) / 4) |

### Index (Z)

The Z channel is optional, with three modes:
- **Ignore.**
- **Mark:** an index frame and a marker, reporting the counts since the last index.
- **Reset:** count = 0 at Z↑.

Each segment also carries **travel**: the cumulative signed count, never reset. Speeds and move deltas use travel, so they stay right across index resets.

### Frames

| Type | Meaning |
| --- | --- |
| `count` | Interval at a constant count |
| `index` | 1 sample |
| `error` | 1 sample, invalid transition |
| `move` | *Moves and holds* only; from → to, mean speed, min/max, and how many index/error events it absorbed |

- A move whose net change is 0 is shown as **jitter** ("Jitter 599…601").
- Index and error frames inside a move are absorbed into it; their markers stay on the channels.

### Settings

- A, B, Z (optional).
- Decoding: X4 / X2 / X1.
- Index mode.
- Results: Every count / Moves and holds; hold time in ms.
- Units: counts / degrees / revolutions, with lines per revolution (PPR). Counts per revolution = PPR × decoding factor.
- Invert direction.

### Code structure

| Area | Contents |
| --- | --- |
| Core (no SDK) | `QuadratureDecoder` (merged edges → count events), `SegmentBuilder` (events → interval segments), `MoveGrouper`, `EncoderWaveform` (synthetic program for tests and Logic's demo device) |
| SDK glue | Reads A/B/Z in lookahead chunks: `WouldAdvancingToAbsPositionCauseTransition`, then merges edges by sample, so same-sample A+B changes are detected |

## Findings / gotchas

- (Carried over from the stepper analyzer) The Windows multi-config CI build needs `Analyzer.dll` copied next to SDK-linked test executables, or Windows' "DLL not found" box hangs the job.
- (Carried over) The Linux SDK `libAnalyzer.so` is libc++, so SDK-linked test executables build on Windows/macOS only.
- (Carried over) Captures loaded through automation are headless. To see bubbles, use `save_capture` with the analyzer, then open the file in the UI with `--loadFile`.

- **The end-of-capture problem (affects both analyzers):**
  - The SDK never signals the end of a capture; reads past the end block. Logic marks the analyzer done, and finalizes the FrameV2 data table, within milliseconds of that block.
  - After that, a frame lying entirely before the final progress is dropped from the table. Frames reaching the end make it only by winning the race.
  - Solution: `IdleFlusher` emits the open span after the worker waits (100 ms, backing off to 2 s; any change resets it). `MoveGrouper::Tick` emits moves as soon as the hold is established, so nothing stale is pending.
  - The bubbles and legacy export always get the final span; the table sometimes does. Verified in the UI (the 6-moves capture's final "Count 57" bubble runs to the end).
- **Experiments that did not work (don't retry):**
  - A 20 ms or 10 ms flush delay: won the race only 1 in 4 times.
  - Reporting progress only up to the open span's start: no effect.
  - One `CommitResults` per frame: no effect.
- **`DoMoreTransitionsExistInCurrentData()` takes about 250 ms whenever the answer is false.** Never call it per chunk. An earlier trace that blamed `WouldAdvancingToAbsPositionCauseTransition` was wrong: the slow call was `DoMore`. Without it, the whole 7.7 s, 250 MS/s capture is traversed in 172 ms.
- **Logic serves data past the visible end for some captures.** The user's `Session 0.sal` (meta v22, saved by 2.4.46) was read quickly to about 280 s though it ends at 37.9 s (not the glitch filter; tested with it off). The forum captures (v15) end exactly.

## Progress log

- [x] Downloaded the two real encoder captures from the forum thread.
- [x] Core decoder + unit tests (8 cases, 18178 assertions); SDK glue, settings, results, simulation and its SDK-linked test (`bf27691`).
- [x] Fixed test-instance profile seeding (`078d7cb`; same fix in stepper `bac54b7`).
- [x] Python reference decode of both forum captures. It matches the C++ replay and Logic's own table exactly: 6-moves final 57 / 25,573 changes; linear final 111,112 / 111,212 changes; 0 errors.
- [x] End-of-capture handling: `IdleFlusher` + `Tick` + continuation (`4b0fbbb`). Ported to the stepper (`23efce3`, pushed, CI green).
- [x] Verified in the isolated Logic UI (screenshot `%TEMP%/salre/quad/ui_6moves.png`).
- [x] README (`43711e5`); Linux/WSL build and tests pass.
- [ ] **Current step:** ask the user to publish to GitHub (proposed `cinderblock/saleae-quadrature-analyzer`) and to install, which needs their Logic closed. The stepper's installed copy also needs updating to `23efce3`.

## Open questions for the user

1. Publish to GitHub as `cinderblock/saleae-quadrature-analyzer` (public)? Recommended.
2. Install the quadrature analyzer and update the stepper analyzer in your everyday Logic? Both need Logic closed (you close it; then `tools/install.ps1 -Register` in each repo).

## Things not to do

- Don't commit the forum `.sal` files.
- Don't touch the everyday Logic instance (closing it prompts about protected captures). Use the isolated test instance.
