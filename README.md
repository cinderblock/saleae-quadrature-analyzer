# Quadrature Encoder analyzer for Saleae Logic 2

A low-level analyzer for [Saleae Logic 2](https://www.saleae.com/) that decodes an incremental quadrature encoder **live, as you capture**. It takes channels A and B, plus an optional Z (index) channel, and reports:

- the count, or position in degrees or revolutions;
- direction and speed;
- index pulses, with the counts travelled between them;
- skipped states.

## Decoding

(A, B) walks the Gray sequence 00 → 10 → 11 → 01 → 00 when A leads B. That is forward, unless *Invert direction* is set. Each step between neighboring states is one count in **X4** decoding.

| Decoding | Counts on | Counts per line |
| --- | --- | --- |
| X4 | every A and B edge | 4 |
| X2 | every A edge | 2 |
| X1 | A edges while B is low (A↑ forward, A↓ backward) | 1 |

X2 and X1 count specific edges rather than dividing the X4 count. That keeps them exact when the shaft dithers back and forth on an edge.

If A and B change on the **same sample**, a state was skipped: the signals moved too fast for the sample rate, or a glitch hit both lines. The direction is then unknown. The analyzer reports a *Skipped state* error, puts an error marker on A, and leaves the count unchanged instead of guessing. If it happens often, raise the sample rate or enable Logic's glitch filter on A and B.

With Z selected, each rising edge on Z is an index pulse.
- In *Mark* mode, it reports the counts travelled since the previous pulse, which makes a quick lines-per-revolution check.
- In *Reset* mode, it also restarts the count at 0.
- *Travel* (in the data table) is the cumulative count that is never reset. Speeds use it.

## Results

Each result spans a time during which the count held one value.

| Frame type | Meaning |
| --- | --- |
| `count` | The count over the span |
| `index` | An index pulse (one sample long) |
| `error` | A skipped state (one sample long, shown as an error) |
| `move` | With *Results: Moves and holds*: a run of quick counts shown as one result, e.g. `Move 0 → 2048 (+2048 in 0.5 s, 4096 counts/s)`. Index pulses and errors inside it are counted in it. |
| `jitter` | A move that ended where it started, e.g. an encoder dithering on an edge |

A fast move is thousands of narrow frames. When they're too narrow to label, Logic packs the labels side by side instead of placing each one at its frame, so the numbers you see are not where they happened. Use *Moves and holds*, or zoom in, to read moves. You can add the analyzer twice to get both rows.

Data table columns (FrameV2):

| Column | Meaning |
| --- | --- |
| `count` | The count (for a move or jitter: where it ended) |
| `position` | The count in the display units |
| `travel` | Cumulative signed counts, never reset by index pulses |
| `direction`, `rate` | Direction and counts/s: for a count span, implied by the step that ended it; for a move, its mean |
| `since_index` | Index pulses only |
| `from`, `delta`, `min`, `max`, `index_pulses`, `errors` | Moves only |

### The last result of a capture

The Analyzer SDK never tells an analyzer that a capture has ended; reading past the last sample just waits. So the final count would never close as a result. When the analyzer has been waiting for data for a moment, it emits the open count up to the data read so far.

The bubbles therefore always show the final count. Logic's data table and export, however, are finalized within milliseconds of the analyzer reaching the end, so the last row is often missing there.

During a live capture, pauses in the incoming data can split a long hold into a few results with the same count. The wait before splitting starts at 100 ms and backs off to 2 s while the hold continues.

## Settings

| Setting | Default | Notes |
| --- | --- | --- |
| A, B | — | Forward is A leading B. |
| Z (index) | None | Optional. |
| Decoding | X4 | X4 / X2 / X1. |
| Index | Mark | Mark / Reset count to 0 / Ignore. Only used with Z selected. |
| Results | Every count | *Moves and holds* groups quick successive counts into one move result. |
| Hold time (ms) | 20 | With *Moves and holds*: a count held at least this long is a hold and ends a move. |
| Display units | Counts | Or degrees / revolutions. |
| Lines per revolution | 1024 | Encoder PPR. Counts per revolution = lines × decoding factor. |
| Invert direction | off | Count B leading A as forward. |

## Installing

1. Build it (below), or download a build from the GitHub Actions artifacts or releases.
2. In Logic 2, open *Preferences → Custom Low Level Analyzers* and point it at the folder holding `QuadratureEncoderAnalyzer.dll` (`.so` on macOS and Linux).
3. Restart Logic. *Quadrature Encoder* appears in the analyzer list.

On Windows, after building, `./tools/install.ps1` copies the DLL to `%LOCALAPPDATA%\Saleae Logic Analyzers\QuadratureEncoder`. Logic locks the DLL it has loaded, so this keeps rebuilds possible while Logic is open. Close Logic before reinstalling over a loaded copy.

With Logic closed, `./tools/install.ps1 -Register` also adds that folder to Logic's custom analyzer paths. It leaves the rest of Logic's settings untouched and keeps a backup of them.

## Building

The build uses CMake and fetches the [Saleae Analyzer SDK](https://github.com/saleae/AnalyzerSDK) automatically.

On Windows (Visual Studio 2022 with the C++ tools, plus Ninja):

```powershell
./build.ps1 -Test   # Release build + tests; the analyzer is build/release/Analyzers/QuadratureEncoderAnalyzer.dll
```

Anywhere else:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

`.github/workflows/build.yml` builds Windows, macOS and Linux binaries and runs the tests. A tag publishes them as a release.

## Development

- **`src/core/`:** the decoder, with no Analyzer SDK dependency:
  - `QuadratureDecoder`: Gray state machine → count spans, index and error instants;
  - `MoveGrouper`: *Moves and holds*;
  - `EncoderWaveform`: a synthetic encoder program, also used by Logic's demo device.
- **`src/IdleFlusher.h`:** emits the open span while the analyzer waits for data (see above).
- **`tests/core_tests.cpp`:** doctest unit tests. These include decoding the synthetic program at 1–250 MHz and checking the count exactly at random times, plus the X2/X1 relations to X4.
- **`tests/simulation_tests.cpp`:** runs the demo-device generator against the real Analyzer SDK library (Windows and macOS; the Linux SDK library needs a libc++ toolchain).
- **`tools/replay_csv.cpp`:** replays a Logic digital CSV export (*File → Export Data → CSV*) through the decoder:

  ```sh
  quadrature_replay digital.csv --rate 250000000 --columns 0,1 --group-ms 20
  ```

- **`tools/logic-test-instance.ps1`:** starts a separate Logic 2 instance with the current build loaded, the automation API on port 10431, and device scanning off. It never touches your normal Logic session. `-LoadFile capture.sal` opens a capture in its UI.

The decoder was checked against two real encoder captures from the Saleae forum thread [Analyser for quadrature encoder](https://discuss.saleae.com/t/analyser-for-quadrature-encoder/2556): a linear encoder, and a "6 moves" session, both at 250 MS/s. Final counts, change counts and the absence of skipped states matched an independent reference decoder exactly, in the replay tool and inside Logic. Those captures aren't redistributed here; download them from the thread into `testdata/` (gitignored) to reproduce.

## License

MIT. Based on Saleae's [SampleAnalyzer](https://github.com/saleae/SampleAnalyzer) template.
