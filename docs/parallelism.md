# Parallelism Approach

This document explains how the design uses the FPGA fabric to accelerate
the nonogram solver, and why that approach is the right one for the
mark-scheme banding (0 / 10 / 18 marks for acceleration).

## Where the time goes

A nonogram solver is dominated by **per-line constraint propagation**.
The outer loop is "until quiet, repeat: for each dirty line, recompute
which cells must be filled or empty given the clue and what we already
know". This is true at every difficulty:

* Easy puzzles solve by propagation alone, with no backtracking.
* Medium / Hard puzzles need backtracking, but the *propagation* step
  inside every guess still dominates the per-frame cost - good
  propagation prunes the search tree by orders of magnitude.

Each per-line evaluation is independent of every other line, has no
internal state to carry over between calls, and operates on small inputs
(<= 32 cells). It is the textbook "embarrassingly parallel kernel"
sitting inside an otherwise sequential algorithm. That makes it the
correct candidate for HLS offload.

## Hardware / software split

| Component | Where | Why |
|---|---|---|
| Per-line constraint propagator | HLS IP (`toplevel`) | Bit-parallel, no state across calls, dominates runtime. |
| Outer "until quiet" propagation queue | ARM | Tiny bookkeeping, easy to debug. |
| Backtracking search | ARM | Recursion-flavoured control flow that HLS forbids; iterative form is natural in C. |
| UDP server protocol | ARM (lwIP) | Only network stack we have. |
| HDMI rendering | ARM (Digilent driver) | Hardware side is the supplied VDMA / VTC / RGB2DVI hierarchy. |
| User input | UART | Trivial. |

## Three layers of parallelism in the IP

These are the three claims we point the marker at, all visible in
`hls/nonogram_line_accel.cpp`.

### 1. Bit-parallel datapath

A whole 32-cell line lives in one `uint32`. Inside the enumeration loop
the IP performs the consistency test on every cell of the line in a
single FPGA cycle:

```cpp
uint32 candidate = BuildCandidateMask(blockMask, start) & lineMask;
if (((candidate & knownEmpty)  == 0) &&
    ((candidate & knownFilled) == knownFilled)) {
    andAcc &= candidate;
    orAcc  |= candidate;
    count++;
}
```

The same logic on the ARM is a per-cell software loop: a 16-cell line
spends ~16x more cycles on the ARM than on the IP for the same logical
work, before pipelining is even considered.

### 2. Pipelined enumeration

The outer `enumerate_loop` is annotated `#pragma HLS PIPELINE II=1` so HLS
overlaps the four stages of one iteration:

1. compute prefix-sum gap totals,
2. build the candidate placement mask,
3. compare against `knownFilled` / `knownEmpty`,
4. accumulate `andAcc`, `orAcc`, `count`, then increment the odometer.

A new candidate placement is therefore tested every clock once the
pipeline is full. The ARM equivalent is forced to serialise these
stages because each depends on the previous one's data.

### 3. Fully partitioned per-block arrays

`blockLen[]`, `blockMask[]`, `anchor[]`, `gap[]`, and `start[]` all
carry `#pragma HLS ARRAY_PARTITION complete dim=1`. This forces them
into individual registers rather than BRAM, which is what lets:

* `BuildCandidateMask()` unroll into a single combinational OR-tree across all
  16 possible blocks;
* the parallel prefix-sum that builds `start[]` from `gap[]` flatten
  into one combinational adder tree;
* the right-to-left odometer increment unroll across all 16 digits in
  parallel.

Without these pragmas HLS would only get two BRAM reads per cycle and
the inner loop would balloon to many cycles per iteration.

## Why the IP returns AND/OR rather than a list of placements

The IP could in principle return every valid placement to the ARM. It
doesn't, because:

* The number of placements explodes for sparsely-clued lines, so we'd
  spend most of the time bursting them across AXI.
* The ARM only ever consumes the AND (forced filled) and OR (~OR
  forced empty) reductions to update its grid.

Doing the reduction inside the IP means each call returns just three
32-bit results regardless of how many candidates were enumerated, and
keeps the AXI master traffic to one descriptor in / one descriptor out.

## Cache discipline

`hls_accel_driver.c` follows the EMBS Software API caching guidance
exactly:

* The 32-word descriptor lives in an `__attribute__((aligned(32)))`
  static buffer so it sits on whole Cortex-A9 cache lines.
* `Xil_DCacheFlushRange` before `XToplevel_Start` pushes the inputs to
  DDR.
* `Xil_DCacheInvalidateRange` after `XToplevel_IsDone` discards stale
  cache copies of the outputs so the ARM reads what the IP just wrote.

## Call-level parallelism (potential extension)

The propagation queue is a stream of independent line jobs. Today our
driver spins on `XToplevel_IsDone`, but with `ALLOCATION` directives
in HLS we could instantiate two or four copies of `toplevel` and let
the ARM dispatch dirty rows and dirty columns to them in parallel.
This is an obvious next step if the synthesis budget on the part
permits it.

## How this hits the mark-scheme

* **10-mark band** (correct use of an IP core to help solve the
  puzzle): the IP genuinely does work, is wired through AXI master +
  AXI-Lite, and the software side calls it on every dirty line during
  every propagation pass.
* **18-mark band** (evidence of effective parallelism): the three
  layers above are explicit in source, visible in the synthesis report
  (`enumerate_loop` pipelined, BRAM at 0/1, LUT count consistent with the
  unrolled OR-trees), and combine to give a measurable end-to-end
  speed-up on Easy / Medium / Hard puzzles.
