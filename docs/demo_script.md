# Demo Script

Demo is marked out of 50:

| Marks | What |
|---:|---|
| 2  | Request specified puzzles |
| 6  | HDMI display: grid, clues, solved cells |
| 6  | Easy 5x5..8x8 |
| 6  | Medium 10x10..14x14 |
| 4  | Correct use of FreeRTOS |
| 18 | HLS acceleration with parallelism |
| 8  | Hard 16x16+ |

## Pre-demo checklist

* Board powered, HDMI to monitor, Ethernet in the **raised** lab port.
* Serial open at 115200.
* FPGA programmed, application launched.
* Run one easy puzzle as a sanity check.

## 1. Request a specified puzzle (2 marks)

The marker dictates tier / size / seed. Type at the prompts:

```
Tier (0-3): 1            <- Easy
Size index (0-15): 0     <- 5x5
Seed (blank for 0):
```

The HDMI shows the grid and clues as soon as `PUZZLE_INFO` arrives.
Serial confirms the puzzle dimensions.

## 2. HDMI display (6 marks)

The render shows:

* Row clues to the left of the grid, bottom-aligned per row.
* Column clues above the grid, right-aligned per column.
* The grid itself with one-pixel separator lines:
  * **White block** = filled cell.
  * **Mid-grey block** = known empty.
  * **Near-black block** = unsolved (only visible mid-solve).

Point at one of each kind to show the empty/unsolved distinction the
brief asks for.

## 3. Easy 5x5..8x8 (6 marks)

```
1 0 0    -> Easy 5x5
1 1 0    -> Easy 6x6
1 2 0    -> Easy 7x7
1 3 0    -> Easy 8x8
```

Each finishes well under a second. Talk while it runs:

> "Easy tier is guaranteed to be solvable by constraint propagation
> alone. The HLS IP returns AND/OR/count for each line, the
> propagation queue empties with no contradiction, and there is no
> backtracking."

## 4. Medium 10x10..14x14 (6 marks)

```
2 4 0    -> Medium 10x10
2 5 0    -> Medium 12x12
2 6 0    -> Medium 14x14
```

Most finish in under a second. Harder seeds may need a small
backtracking depth; the serial line for those takes noticeably
longer between "Puzzle:" and "CORRECT".

## 5. FreeRTOS (4 marks)

While a Medium puzzle is solving, point out:

* Serial echoes new keystrokes (UI task is alive).
* HDMI updates as the solver progresses.

> "There are four genuinely concurrent things: the user typing, lwIP
> receiving, the solver crunching, and the HDMI redraw. Each is its
> own task. `NetTask` runs at DEFAULT+1 because losing a server reply
> costs wall-clock time against the 60 s budget. The shared
> `PuzzleState` is protected by a *mutex* (not binary semaphore) so
> we get priority inheritance when `NetTask` blocks behind `SolverTask`."

Point them at `vitis/src/freertos_tasks.c` for the topology.

## 6. HLS acceleration (18 marks)

This is the big one. Walk them through the three claims in
[parallelism.md](parallelism.md):

1. **Bit-parallel datapath**. Show the AND/OR consistency lines in
   `hls/nonogram_line_accel.cpp` (search for `cand & knownEmpty`).
   Every clock all 32 cells are tested at once.
2. **Pipelined enumeration loop** (`enumLoop:` with `PIPELINE II=1`).
   HLS overlaps prefix-sum / mask build / consistency check / accumulate.
3. **Fully partitioned per-block arrays** (`ARRAY_PARTITION complete`).
   Without these the OR-tree in `BuildMask` would serialise at two
   BRAM reads per cycle.

Then point at `hls/nonogram_line_hls/solution1/syn/report/toplevel_csynth.rpt`:

* `enumLoop` is pipelined in the Performance Estimates section.
* BRAM should be 0 or 1 (the partitioning is working).

## 7. Hard 16x16+ (8 marks)

```
3 7 0    -> Hard 16x16
3 8 0    -> Hard 18x18
3 9 0    -> Hard 20x20
```

These go into backtracking. 16x16 should finish well inside 60 s.
20x20 may use the full budget. Backup seeds from the example
responses in case anything goes wrong:

```
3 7 2    -> Hard 16x16, seed=2
3 9 4    -> Hard 20x20, seed=4
```

## Wrap-up

If asked "what would you do with more time?":

* Multiple HLS instances dispatched in parallel via `ALLOCATION`.
* Software fallback path so the system still works if HLS fails.

If asked "what's the slowest part?":

* Easy/Medium - the server round-trip dominates.
* Hard - the *number* of guesses dominates, which is exactly why a
  fast per-line propagator is valuable: each guess's propagation pass
  is amortised across far fewer guesses than it would be without HLS.
