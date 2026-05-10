# EMBS Part III - Nonogram Solver

Single-bitfile, single-executable nonogram solver running on the Zybo Z7-10
under FreeRTOS, talking to the EMBS Nonogram server over UDP, with the
per-line constraint propagator accelerated by a custom Vitis HLS IP core.

This folder is meant to be zipped and submitted alongside Parts I and II.
Edit `vitis/src/config.h` (MAC address) and the two repo paths at the top
of `vivado/create_project.tcl` before building.

## Folder layout

```
EMBS_Part3_Nonogram_Solver/
├── README.md
├── vivado/
│   ├── create_project.tcl       # Reproducible Vivado 2025.1 build
│   └── block_design_notes.md    # What the script builds, manually
├── hls/
│   ├── nonogram_line_accel.h    # Top-level interface + descriptor layout
│   ├── nonogram_line_accel.cpp  # Custom IP - the line propagator
│   ├── testbench.cpp            # csim test bench (must pass before synth)
│   └── run_hls.tcl              # Headless HLS build + IP export
├── vitis/
│   └── src/
│       ├── main.c               # Brings up cache/HDMI/HLS, starts FreeRTOS
│       ├── platform.{c,h}       # Cache enable/disable
│       ├── network.{c,h}        # DHCP + lwIP bring-up (Practical-6 pattern)
│       ├── server_protocol.{c,h}# UDP message encode/decode (BE wire format)
│       ├── nonogram_solver.{c,h}# Constraint prop + iterative backtracking
│       ├── hls_accel_driver.{c,h}# Thin XToplevel_* wrapper
│       ├── graphics.{c,h}       # 1440x900 HDMI rendering, no 3rd-party fonts
│       ├── freertos_tasks.{c,h} # ui/net/solver/gfx tasks + queues + mutex
│       └── config.h             # All the build-time knobs
└── docs/
    ├── design_explanation.md    # The "argue your design" notes
    ├── demo_script.md           # What to do during the in-person demo
    └── testing_plan.md          # Bottom-up unit tests, then integration
```

## Pre-requisites (one-time, on the lab machine or your own setup)

1. Vivado / Vitis / Vitis HLS 2025.1, available on `$PATH`
   (per practical 1a: `/opt/york/cs/net/xilinx_vivado-2025.1-x86_64-1/...`).
2. `LM_LICENSE_FILE=2100@cslm0.its.york.ac.uk` (only for HLS, see practical 3a).
3. The Digilent Zybo Z7-10 board files in Vivado.
4. A local clone of the EMBS HDMI helper repo:
   `git clone https://github.com/RTSYork/zybo-z7-hdmi.git ~/embs/zybo-z7-hdmi`
   This is the EMBS-recommended HDMI repo; per the assessment rules the
   restriction on third-party IP excludes the EMBS-provided HDMI helper.
5. The lab Ethernet cable in the **raised** port (per Practical 2 / EMBS
   Student Network), not the one with the green sticker.

## Build & run

The end-to-end flow is:

```text
HLS    -> synth + export IP catalog (-> hls/nonogram_line_hls/...)
Vivado -> create project, source HDMI tcl, add HLS IP, generate bitstream,
          export hardware (.xsa)
Vitis  -> import platform from .xsa, create FreeRTOS application, set
          BSP options (lwip220, SOCKET_API, DHCP, xiltimer), import
          HDMI software dir, add this src/ folder, build, run on board
```

Step-by-step:

### 1. Build the HLS IP core

```bash
cd EMBS_Part3_Nonogram_Solver/hls
vitis_hls -i run_hls.tcl
# produces ./nonogram_line_hls/solution1/impl/ip
```

The `csim_design` step inside `run_hls.tcl` runs the testbench. If
anything in the testbench fails the build aborts (see
`docs/testing_plan.md` for what each test exercises).

### 2. Build the Vivado hardware

```bash
cd EMBS_Part3_Nonogram_Solver/vivado
# Edit the three paths at the top of create_project.tcl:
#   proj_dir, hdmi_repo_dir, hls_ip_repo_dir
vivado -mode batch -source create_project.tcl
# Then in the GUI (or another batch script):
#   launch_runs synth_1 -jobs 4
#   launch_runs impl_1 -to_step write_bitstream -jobs 4
#   write_hw_platform -fixed -include_bit -force \
#                     -file <proj_dir>/design_1_wrapper.xsa
```

If you prefer the GUI, follow `vivado/block_design_notes.md` step by
step. The file is written to be a literal manual replacement for the
TCL script.

### 3. Build the Vitis application

1. Open Vitis 2025.1 with a fresh workspace.
2. `File -> New Component -> Platform`. Browse to the .xsa from step 2.
   OS = `freertos`, Processor = `ps7_cortexa9_0`.
3. Open the Platform's `Settings -> vitis-comp.json -> standalone_ps7_cortexa9_0
   -> Board Support Package`. Enable `lwip220`. Set `lwip220_dhcp = true`,
   `lwip220_api_mode = SOCKET_API`. Under `xiltimer`, enable
   `XILTIMER_en_interval_timer` and set `XILTIMER_tick_timer = ps7_scutimer_0`.
   Save.
4. `File -> New Component -> Application Project`. Pick the platform from
   step 2 and the `freertos_ps7_cortexa9_0` domain. Skip the template
   selection (blank C project).
5. Right-click `src` in the new application -> `Import -> Folders...`,
   import:
   * The contents of `EMBS_Part3_Nonogram_Solver/vitis/src/`
   * `<...>/zybo-z7-hdmi/software/zybo_z7_hdmi/` (provides `display_ctrl.{c,h}`,
     `vga_modes.{c,h}` and the supporting Digilent code).
6. Edit `config.h` and put **your** assigned EMBS MAC address in
   `EMBS_MAC_LAST` (the table is at the EMBS Student Network page).
7. Build the application. If any `.c` is missing from the build, open
   `Settings -> UserConfig.cmake -> Compile Sources` and add it.

### 4. Program the board

* Connect HDMI TX (top-left port) to a monitor and Ethernet to the
  raised lab port.
* Open `screen /dev/ttyUSB1 115200` (or `/dev/ttyUSB2`).
* In Vitis, `Run` the application. The serial prints `IP: x.x.x.x`
  once DHCP succeeds, followed by the prompt menu.
* Type `1`, `0`, blank at the prompts (Easy / 5x5 / auto-seed) for
  the safest smoke test. The HDMI shows the puzzle, the serial prints
  `Puzzle: ...` then `CORRECT (XXX ms)`.

### Re-building after HLS changes

Per the EMBS knowledge base recipe (`hls-knowledge-base#when-you-change-your-hls`):

1. `vitis_hls -i hls/run_hls.tcl`
2. In Vivado: `IP Status -> Refresh IP Catalog -> Upgrade Selected ->
   Generate Output Products -> Generate Bitstream -> Export Hardware`.
3. In Vitis platform: `vitis-comp.json -> Switch / re-read XSA`. Clean
   build the application.

### Networking troubleshooting (server request times out)

Common causes in order of likelihood:

1. **BSP is not in `SOCKET_API` mode.** `NetTask` uses `lwip_socket` /
   `lwip_sendto` / `lwip_recvfrom`, which only exist when
   `lwip220_api_mode = SOCKET_API`. If you forgot this option the
   build will either fail to link the `lwip_*` symbols or it will
   build but `lwip_socket()` returns -1 at runtime.
   Fix: open the platform's `vitis-comp.json`, navigate to
   `freertos_ps7_cortexa9_0 -> standalone -> Board Support Package
   -> lwip220` and confirm `lwip220_api_mode = SOCKET_API` and
   `lwip220_dhcp = true`. Regenerate BSP, clean-build everything.
2. **MAC address not in the EMBS DHCP table.** If your username isn't
   listed on the EMBS Student Network page, DHCP silently fails and
   you never see `IP:` on the serial. Ask the demonstrator to add
   you.
3. **Wrong Ethernet jack.** EMBS network is on the **raised** port
   (numbered `CSE/X/XXX-XX`), NOT the flat one with the green
   sticker. The green port has no route to `192.168.10.1`.
4. **Local UDP port collision.** `NONOGRAM_LOCAL_PORT = 51050` in
   `config.h`. Pick anything in 49152-65535 if you suspect a clash.
5. **lwIP heap exhausted.** Bump `lwip220 -> pbuf_pool_size` in BSP
   settings to 512 if you see `socket init failed`.
6. **Sending from a fresh PCB each call.** Our code keeps one socket
   open via `gSock`. If you ever change that and start seeing "No
   active session - send REQUEST_INFO first" in the server's ERROR
   reply, you've reintroduced the bug.

## Demo prep

`docs/demo_script.md` walks through the marker rubric (`2 + 6 + 6 + 6 +
4 + 18 + 8`) and what to show for each. `docs/design_explanation.md`
contains the "argue the FreeRTOS choice" and "argue the parallelism
choice" speeches. `docs/testing_plan.md` is the bottom-up debugging
order for when something stops working two days before the demo.

## Notes

* **No third-party IP** is used inside the HLS core (the file ships with
  a hand-written 8x16 ASCII font; no font library, no external
  bitmaps). The Digilent rgb2dvi/VTC/VDMA IP is what the EMBS HDMI page
  prescribes -- the assessment text excludes it from the "no third-party
  IP" rule, see `docs/design_explanation.md` for the marker-facing
  argument.
* **Single bitfile, single executable** -- everything is parameterised
  at runtime via the UART prompt + `config.h`. There is no per-puzzle
  recompilation.
* `MAX_N = 32` (largest server puzzle) means a row fits in one `uint32`,
  which is exactly what the HLS line accelerator wants.
* Anything labelled `ASSUMPTION` in source is also flagged in
  `docs/design_explanation.md`.
