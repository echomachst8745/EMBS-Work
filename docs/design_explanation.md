# Design Explanation

What to read out (or paraphrase) when the markers ask "why did you do
it this way?". The detailed parallelism story has its own document at
[parallelism.md](parallelism.md); this file covers the rest.

## Hardware/software split

Per-line constraint propagation is the kernel that dominates runtime,
is bit-parallel, and is independent across lines. It goes in HLS.
Everything else stays on the ARM. See [parallelism.md](parallelism.md)
for the full argument.

## FreeRTOS task layout

Four genuinely concurrent things in this system:

1. The user typing on UART.
2. lwIP receiving and dispatching UDP packets.
3. The solver crunching propagation + backtracking on the ARM (with
   short synchronous calls into the FPGA).
4. The HDMI redraw, which is CPU-intensive and bursty.

Bare-metal this is a state machine in `main()`. With FreeRTOS each is
its own task and we can talk about scheduling and blocking properly:

| Task | Priority | Stack | Why |
|---|---|---|---|
| `xemacif_input_thread` | DEFAULT | 1024 w | lwIP must drain its FIFO. |
| `NetworkThread` | DEFAULT | 1024 w | DHCP timers, runs forever. |
| `NetTask` | DEFAULT+1 | 2048 w | Highest priority - missing a server reply costs wall-clock seconds against the 60 s budget. |
| `SolverTask` | DEFAULT | 4096 w | CPU-bound, large stack for backtrack frames. |
| `GfxTask` | DEFAULT | 4096 w | Blocks on `gGfxSem`; large stack for frame-buffer work. |
| `UiTask` | DEFAULT | 1024 w | Polls UART with a 20 ms `vTaskDelay` so it doesn't spin. |

Inter-task communication:

* `gParamsQ` - `ui -> net`, FIFO of `PuzzleParams` (tier / size / seed).
* `gSolveQ` - `net -> solver`, simple "go" signal.
* `gSubmitQ` - `solver -> net`, packed bitmap to ship.
* `gGfxSem` - binary semaphore meaning "redraw now".
* `gDoneSem` - binary semaphore meaning "this puzzle round is finished",
  so the UI can prompt for the next one.
* `gPuzzleMtx` - **mutex** (not binary semaphore) protecting the shared
  `PuzzleState`. Mutexes have priority inheritance, which avoids a
  scenario where `NetTask` (high) is blocked on a low-priority holder
  while `SolverTask` (mid) preempts.

## Server protocol

`server_protocol.{c,h}` is a literal port of the byte tables in the
assessment information page. We keep one UDP socket open for the whole
session, bound to a fixed local port (`NONOGRAM_LOCAL_PORT`), so the
server keeps recognising us as the same client. We use the lwIP
**socket API**, not the raw `udp_*` API: raw sends from arbitrary
FreeRTOS tasks are not thread-safe in the BSP we use, and silently
sending from random ephemeral ports immediately reproduces the "no
active session" error in the assessment clarifications.

## Solver algorithm

Constraint propagation:

1. Mark every row and column dirty.
2. Pop a dirty line, ask the HLS IP for `(and, or, count)`.
3. `and` gives newly-forced filled cells. `~or` gives newly-forced
   empty cells. Both intersected with "things we don't already know"
   tell us what's *new* this pass.
4. For every cell that changed, mark the perpendicular line dirty.
5. Repeat until the queue empties. If any line returned `count == 0`,
   contradiction.

Iterative backtracking:

* Pick the unknown cell in the most-known row, leftmost first.
* Snapshot the four bitmap-of-bitmaps state into a stack frame
  (~512 bytes).
* Try FILLED, propagate. On contradiction, restore and try EMPTY.
* If both fail, pop and continue.
* `SOLVER_MAX_BACKTRACK = 400` frames sized for hard 20x20.
* Time-bounded by `SOLVE_BUDGET_MS = 55000` so we always submit before
  the 60 s server cap.

## "No third-party IP" defence

The only externally-sourced HDL or IP is the HDMI hierarchy from the
EMBS-provided `zybo-z7-hdmi` repo (Digilent rgb2dvi, AXI VDMA, VTC -
all Xilinx IPs explicitly endorsed by the EMBS HDMI page). The custom
IP work is 100% in `hls/nonogram_line_accel.cpp`.
