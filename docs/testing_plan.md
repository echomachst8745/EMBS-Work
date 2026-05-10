# Testing Plan

Four moving parts to test: solver logic, HLS IP, lwIP/server plumbing,
FreeRTOS+HDMI integration. Test bottom-up.

## Stage 1: Pure-C solver unit tests

Confirms that `nonogram_solver.c` works without the HLS IP or the
network. Replace `HlsRunLine` with a software reference and feed the
canonical clue streams from `example_server_responses.md`:

```c
uint8_t example5x5Easy[] = {
    0x01,0x01, 0x01,0x01, 0x01,0x01, 0x02,0x01,0x01, 0x01,0x01,
    0x01,0x01, 0x00, 0x02,0x03,0x01, 0x00, 0x01,0x01
};
PuzzleState p;
SolverLoadClues(&p, 1, 0x10, 5, 5, example5x5Easy, sizeof(example5x5Easy));
assert(SolverSolve(&p, 1000));
assert(SolverValidate(&p));
```

Repeat for the 8x8 / 10x10 / 12x12 / 16x16 / 20x20 examples. All
should solve and validate.

## Stage 2: HLS C-simulation

Run `hls/run_hls.tcl` to execute `csim_design`, which runs
`testbench.cpp`. The testbench builds a fake DDR array, calls
`toplevel`, and compares the outputs against a small table of
hand-calculated expected values for representative line shapes
(empty, single clue with overlap, contradicting knowns, multi-block,
clue-can't-fit, 32-cell edge case).

Expected output:

```
5/blocks=[]/known=0          PASS  ...
10/[7]/empty                 PASS  ...
...
All line-accelerator tests passed.
```

## Stage 3: HLS synthesis sanity

After `csynth_design`:

* Open `hls/nonogram_line_hls/solution1/syn/report/toplevel_csynth.rpt`.
* `enumLoop` pipelined (II=1 target; II>=1 is acceptable for timing).
* `LUT` count in the few-thousands; `BRAM` 0 or 1. If BRAM > 5 a
  partitioning pragma is missing.

## Stage 4: Vivado wiring sanity

After `Generate Bitstream`, in the Address Editor confirm
`toplevel_0/s_axi_AXILiteS/Reg` is assigned. In Vitis, search the
project for `XPAR_TOPLEVEL_0_BASEADDR` after re-reading the XSA. If
absent, regenerate the BSP.

## Stage 5: HLS smoke test on the board

`HlsInit()` fires a harmless run (length=1, B=0) and confirms the
version tag. Returning false means the AXI slave isn't wired up
(probably a missed Run Connection Automation step).

## Stage 6: lwIP smoke test

Use the example "Easy 5x5 seed=1" puzzle. Send REQUEST_INFO,
REQUEST_CHUNK, expect the byte stream documented in
`example_server_responses.md`. If you don't, you're either on the
wrong network port, your MAC is wrong, or you're creating a new socket
per send instead of reusing one.

## Stage 7: End-to-end demo dry run

Walk through `docs/demo_script.md` with a stopwatch. Expected:

* Easy 5x5 - "instant", server time <50 ms.
* Easy 8x8 - <1 s, server time <200 ms.
* Medium 10x10 - typically <500 ms.
* Medium 14x14 - 1-3 s.
* Hard 16x16 - 5-30 s depending on seed.
* Hard 20x20 - can use the full budget.

If any stage regresses, walk back through 1-6 to localise.
