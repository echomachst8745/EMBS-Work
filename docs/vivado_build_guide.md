# Vivado Block Design - Step by Step

This is the manual GUI walkthrough that builds the same design as
`vivado/create_project.tcl`. Use it the first time you build the
project so you understand every block, or if the TCL script fails
and you need to recover.

Target: **Vivado 2025.1**, board **Zybo Z7-10** (`xc7z010clg400-1`).

> Estimated time: 30-45 min on a fresh machine, plus 15-30 min for the
> `Generate Bitstream` step at the end.

## 0. Pre-requisites checklist

Before you even open Vivado:

- [ ] Vivado 2025.1 on `$PATH` (per Practical 1a:
      `/opt/york/cs/net/xilinx_vivado-2025.1-x86_64-1/2025.1/Vivado/bin`).
- [ ] License variable set:
      `LM_LICENSE_FILE=2100@cslm0.its.york.ac.uk`.
- [ ] Digilent Zybo Z7-10 board files installed
      (Vivado will offer to download them on first project creation;
      see Practical 1a).
- [ ] EMBS HDMI helper repo cloned **outside any Vivado project
      directory**:
      ```bash
      mkdir -p ~/embs && cd ~/embs
      git clone https://github.com/RTSYork/zybo-z7-hdmi.git
      ```
- [ ] HLS IP already built:
      ```bash
      cd EMBS_Part3_Nonogram_Solver/hls
      vitis_hls -i run_hls.tcl
      ```
      This produces an exported IP catalog at
      `hls/nonogram_line_hls/solution1/impl/ip`. The directory you'll
      add as an "IP Repository" later is `EMBS_Part3_Nonogram_Solver/hls`
      (Vivado searches subdirectories for IP).
- [ ] At least 5 GB of free disk space (synthesis is greedy - see the
      EMBS "Disk Usage and the Xilinx Tools" page).
- [ ] At least 500 MB of swap so the Vivado GUI doesn't OOM mid-synth.

## 1. Create the Vivado project

1. Launch Vivado: `vivado &`.
2. On the welcome screen click **Create Project**, then **Next**.
3. **Project Name** = `embs_nonogram`. **Project Location** =
   somewhere with no spaces, e.g.
   `~/embs/embs_nonogram_vivado`. Tick **Create project subdirectory**.
   Click **Next**.
4. Project type: **RTL Project**. **Tick** "Do not specify sources at
   this time". Click **Next**.
5. Default Part / Boards page: switch to the **Boards** tab.
   * Vendor: `digilentinc.com`.
   * If `Zybo Z7-10` doesn't appear, click **Refresh** in the top
     right and wait ~30 s for Vivado to refresh the board list.
   * Select the row for `Zybo Z7-10` (Board Rev = whatever the lab
     currently has, usually 2.0). Click the **download** icon in the
     Status column if needed.
   * Click **Next**, then **Finish**.

Vivado now has an empty project targeting the right FPGA.

## 2. Register the IP repositories

Both the HDMI helper and your custom HLS core are external IP, so
Vivado needs to be told where to find them.

1. **Settings** (gear icon, top left of the main toolbar).
2. In the left pane: **Project Settings -> IP -> Repository**.
3. Click the **+** above the IP Repositories list, then **Add
   Repository...**.
4. Browse to `~/embs/zybo-z7-hdmi/hardware/zybo_z7_hdmi_repo` and
   click **Select**.
   * Vivado should pop up a confirmation that **2 IPs and 1 interface
     definition** were added (the Digilent rgb2dvi and an axi-stream
     to video helper).
   * If it says "0 IPs found", you pointed at the wrong directory or
     the repo is inside a Vivado project (it must not be).
5. Click **+** again and add `EMBS_Part3_Nonogram_Solver/hls`
   (the directory containing your HLS project subfolder, *not* the
   `nonogram_line_hls/...` itself).
   * Vivado should report **1 IP added**: `Toplevel` (or the display
     name from `run_hls.tcl`, "Nonogram Line Accelerator").
6. Click **OK** to close Settings.

If the HLS IP isn't found, check that `hls/nonogram_line_hls` exists
and you actually exported the IP (`export_design -format ip_catalog`
inside HLS).

## 3. Create the block design

1. **Flow Navigator** (left sidebar) -> **IP INTEGRATOR -> Create Block
   Design**.
2. **Design name** = `design_1` (keep the default - the Vitis platform
   wrapper expects this name).
3. **Directory** = local to project. Click **OK**.

A blank canvas opens. Save it now: `Ctrl-S`.

## 4. Add and configure the Zynq Processing System

1. In the Diagram canvas, right-click empty space (or use the **+**
   button in the toolbar) -> **Add IP**.
2. Type `zynq` and select **ZYNQ7 Processing System**. Press Enter.
3. A `processing_system7_0` block appears.
4. Click **Run Block Automation** in the green prompt bar at the top
   of the diagram.
   * Apply Board Preset: **ticked**.
   * Cross trigger / pl_clk0: leave defaults.
   * Click **OK**.
   * Vivado now wires up the DDR, FIXED_IO, and the standard board
     I/O assignments (UART1, Ethernet, USB, SD).

5. **Double-click** the `processing_system7_0` block to open the
   re-customise dialog.

### 4a. Enable HP0 and HP1

1. In the left tree: **PS-PL Configuration -> HP Slave AXI Interface**.
2. Tick **S AXI HP0 interface**. (Custom HLS IP master goes here.)
3. Tick **S AXI HP1 interface**. (HDMI VDMA master goes here -
   matches the EMBS HDMI page recommendation.)

### 4b. Enable Timer 0 on EMIO (required for FreeRTOS)

1. In the left tree: **MIO Configuration -> Application Processor
   Unit**.
2. Tick **Timer 0**.
3. In the **IO** column for Timer 0 select **EMIO**.
4. (You should now see `TTC0_WAVE0_OUT` etc. as outputs on the right
   side of the Zynq block when you OK out. We don't connect them.)

### 4c. (Sanity) Verify the rest

* **Peripheral I/O Pins** -> UART1, ENET0, USB0, SD0 should be ticked
  in their MIO columns (the board preset did this).
* **Clock Configuration -> PL Fabric Clocks** -> `FCLK_CLK0` enabled
  at 100 MHz (default). This clocks the AXI interconnects.
* **Interrupts** -> we don't need anything here; HLS uses polling.

5. Click **OK** to close the dialog. The Zynq block now shows the new
   `S_AXI_HP0` and `S_AXI_HP1` ports on its left side.

`Ctrl-S` to save.

## 5. Add the LEDs and Buttons

These are status indicators (not strictly required by the assessment
but very handy for sanity-checking a frozen system in the demo).

1. Switch the centre top tab from **Diagram** to **Board**.
2. Double-click **4 LEDs**. Click **OK** in the IP creation dialog.
   * Vivado adds an `axi_gpio_0` block and connects the LEDs (channel 1).
3. Double-click **4 Buttons**. Click **OK**.
   * Vivado attaches the buttons to channel 2 of `axi_gpio_0`.
4. Switch back to the **Diagram** tab.

Don't run Connection Automation yet - we'll batch it after the HLS
core and HDMI hierarchy are added.

## 6. Add the HDMI hierarchy via the EMBS TCL script

This step has to happen **after** the Zynq block exists and **before**
you run any further Connection Automation, otherwise the script's
internal connections fail.

1. At the bottom of the Vivado main window, find the **Tcl Console**
   (View -> Tcl Console if you can't see it).
2. Type (with the real path to your clone):

   ```tcl
   source /home/yourname/embs/zybo-z7-hdmi/hardware/zybo_z7_hdmi.tcl
   ```

   Press Enter.
3. The script runs for 10-30 s. When it finishes you should see a new
   collapsed hierarchy block called something like `hdmi` containing:
   * an AXI VDMA
   * a Xilinx VTC (Video Timing Controller)
   * the Digilent `rgb2dvi` core
   * a clocking wizard
   * an AXI SmartConnect for the VDMA's master port
4. The script also:
   * Creates external HDMI ports (`hdmi_clk_p/n`, `hdmi_d_p/n`,
     `hdmi_oen`, `hdmi_scl`, `hdmi_sda`) on the right of the diagram.
   * Connects the VDMA master to `S_AXI_HP1` automatically.

> **If `source` errors out** with "no such file" the path is wrong;
> with "Slave segment is not assigned" it usually still worked - read
> on. With "could not find IP repository for digilent.com:rgb2dvi"
> you forgot Step 2 above (the HDMI IP repository wasn't added).

`Ctrl-S` to save.

## 7. Add the custom HLS IP core

1. **Add IP** (right-click canvas or **+** button).
2. Type `toplevel` (or "Nonogram" if you set the display name in
   `run_hls.tcl`).
3. Select your IP - it will be under
   `User Repository -> AXI Peripheral`. Press Enter.
4. A `toplevel_0` block appears. It has:
   * `s_axi_AXILiteS` - the slave control port.
   * `m_axi_MAXI` - the master port that reads/writes the job
     descriptor in DDR.
   * `ap_clk` and `ap_rst_n` - hook up to FCLK_CLK0 / system reset.

## 8. Run Connection Automation (carefully)

Now we have all the pieces. Connection Automation will route AXI
ports for us, but we must steer it so:

* HLS master ends up on **HP0** (not HP1, which is reserved for HDMI).
* HDMI's master is already on HP1 (the script took care of it).
* All slaves go through one `M_AXI_GP0` interconnect.

1. Click **Run Connection Automation** in the green prompt bar.
2. The dialog lists everything that needs wiring:
   * `axi_gpio_0/S_AXI` -> Master = `processing_system7_0/M_AXI_GP0`
   * `toplevel_0/s_axi_AXILiteS` -> Master = `processing_system7_0/M_AXI_GP0`
   * `toplevel_0/m_axi_MAXI` -> Master/Slave dialog: pick
     `processing_system7_0/S_AXI_HP0`. **Important** - if it suggests
     HP1 by default, change it to HP0.
   * Any HDMI control ports left over (VDMA control, VTC control)
     will be listed too; let them go to `M_AXI_GP0`.
3. **Tick All** at the top, double-check the HP0 selection on the HLS
   master, then click **OK**.

Vivado will:

* Insert (or extend) an AXI Interconnect between `M_AXI_GP0` and all
  the slave ports (HLS slave, GPIO slave, VDMA slave, VTC slave).
* Insert (or extend) an AXI Interconnect between the HLS master and
  `S_AXI_HP0`.
* Wire `ap_clk` / `ap_rst_n` of HLS to the system's `FCLK_CLK0` and
  `peripheral_aresetn` from the Processor System Reset block.

If you missed connecting `ap_clk`, the synthesis report will yell
about an unconnected clock pin; come back and Run Connection
Automation again.

`Ctrl-S` to save.

## 9. (Optional) Tidy the layout

* Click the **Regenerate Layout** button (toolbar above the diagram,
  third from the right).
* Drag blocks around if you prefer them in a different arrangement.
  This has no functional impact.

## 10. Validate the design

1. **F6** (or the green ✓ icon) -> **Validate Design**.
2. Expected outcome: "Validation successful. There are no errors or
   critical warnings in this design."
3. Common warnings to ignore:
   * `[PSU-2/3/4]` DDR DQS skew warnings - expected, see Practical 1a.
   * `Slave segment is not assigned into address space` for OCM - safe
     per the EMBS HDMI page.
4. **Real errors to fix**:
   * "Unconnected pin" - run Connection Automation again.
   * "No M_AXI driving slave" - one of the slaves isn't on the
     interconnect; remove and re-add via Connection Automation.

`Ctrl-S` to save the BD.

## 11. Add the HDMI XDC pin constraints

The pin assignments for the HDMI TX connector come from a constraint
file in the HDMI repo.

1. **File -> Add Sources**.
2. Select **Add or create constraints**, click **Next**.
3. Constraint set: **constrs_1** (the default).
4. Click **Add Files**, browse to
   `~/embs/zybo-z7-hdmi/hardware/zybo_z7_hdmi.xdc`, click **OK**.
5. **Tick** "Copy constraint files into project" so future resyntheses
   don't break if you move the HDMI repo.
6. Click **Finish**.

The Sources tab now shows `zybo_z7_hdmi.xdc` under `Constraints ->
constrs_1`.

## 12. Verify the address map

1. From the menu: **Window -> Address Editor**.
2. Expand `processing_system7_0 -> Data`. You should see:

   | Slave | Base | Range |
   | --- | --- | --- |
   | `axi_gpio_0/S_AXI/Reg` | 0x4120_0000 | 64K |
   | `toplevel_0/s_axi_AXILiteS/Reg` | 0x43C0_0000 | 64K |
   | HDMI VDMA control | 0x4300_0000 | 64K |
   | HDMI VTC | 0x4310_0000 | 64K |
   | HDMI clocking wizard etc. | various | various |

3. If anything shows **Unmapped**, click the **Auto-assign
   Address** button (lightning icon) and accept the defaults.
4. Confirm the HDMI VDMA address is `0x4300_0000` - this is what the
   VLAB capture daemon assumes (per the HDMI page).

## 13. Create the HDL wrapper

Block designs aren't directly synthesizable; we need a Verilog wrapper.

1. **Sources** tab.
2. Right-click `design_1` (the .bd file) -> **Create HDL Wrapper...**.
3. Select **Let Vivado manage wrapper and auto-update**, click **OK**.
4. After a few seconds you'll see `design_1_wrapper.v` appear under
   the design.
5. Vivado should auto-set it as the top of the project. If not:
   right-click it -> **Set as Top**.

## 14. Generate Bitstream

1. **Flow Navigator -> PROGRAM AND DEBUG -> Generate Bitstream**.
2. If a "Launch Runs" dialog appears, click **OK** with default
   options.
3. Vivado runs Synthesis, then Implementation, then bitstream
   generation. This is the slow step (~10-30 min on lab machines).
4. Watch the **Design Runs** tab at the bottom for progress.
5. When done, click **Cancel** on the "Bitstream generation
   completed" dialog (we don't need to open the implemented design).

If synthesis fails, the most common causes are:
* **Multiple drivers on a net** - usually a botched Connection
  Automation. Delete the AXI Interconnect blocks and rerun.
* **Could not find IP** - the IP repository moved. Re-add it under
  Settings.
* **Negative slack** - your design is too slow at 100 MHz. Unusual
  for our IP - if it happens, check the HLS report's `Estimated`
  clock period; it should be ~7-8 ns at the 10 ns target.

## 15. Export Hardware (XSA)

This is what Vitis imports.

1. **File -> Export -> Export Hardware...**.
2. Click **Next**.
3. Output: **Include bitstream**. Click **Next**.
4. **Output File** = `design_1_wrapper.xsa` in the project root.
5. Click **Next**, **Finish**.

The `.xsa` is a zip containing the bitstream + the HW handoff
(register addresses, IRQ map, peripheral info). Vitis reads it to
generate drivers like `XToplevel_*`.

## 16. (Optional) Sanity check: open Hardware Manager

1. **Flow Navigator -> Open Hardware Manager**.
2. With the Zybo plugged in via USB and powered on:
   * **Open Target -> Open New Target**, accept defaults.
   * The detected device should be `xc7z010_1` (Practical 1a screenshot).
3. Right-click `xc7z010_1` -> **Program Device**.
4. **Bitstream file** should auto-fill with
   `<project>/embs_nonogram.runs/impl_1/design_1_wrapper.bit`.
5. Click **Program**.
6. The DONE LED on the Zybo should light green.

This proves the bitfile is valid before you go through all of Vitis -
useful if something later goes wrong and you want to isolate the
problem.

## 17. Pass to Vitis

You're done with Vivado. Open Vitis 2025.1, set a workspace, and
follow §3 of the project root `README.md`:

1. New Component -> Platform, point at the `.xsa` you just exported.
2. OS = `freertos`, Processor = `ps7_cortexa9_0`.
3. Open the Platform's `vitis-comp.json`, enable `lwip220` (set
   `lwip220_dhcp = true`, `lwip220_api_mode = SOCKET_API`), enable
   `xiltimer` interval timer with `ps7_scutimer_0`.
4. Build the platform.
5. New Component -> Application Project (blank C). Pick the platform
   and the `freertos_ps7_cortexa9_0` domain.
6. Right-click `src` -> Import -> Folders... and import:
   * `EMBS_Part3_Nonogram_Solver/vitis/src/` (this repo)
   * `~/embs/zybo-z7-hdmi/software/zybo_z7_hdmi/`
7. Edit `config.h` -> set `EMBS_MAC_LAST` to your row in the EMBS
   Student Network table.
8. Build, then `Run` on the board.

## When you re-spin the HLS code

If you change anything in `hls/nonogram_line_accel.{cpp,h}`:

1. `vitis_hls -i hls/run_hls.tcl` (re-runs csim and re-exports IP).
2. In Vivado: open the project. You should see the yellow bar
   "IP Catalog is out-of-date".
   * If not, **Reports -> Report IP Status -> Re-Run Report**.
3. **IP Status** tab: tick `toplevel`, click **Upgrade Selected**.
4. **Generate Output Products -> Global -> Generate**.
5. **Generate Bitstream** again. (Subsequent rebuilds are faster
   because synthesis incrementally re-uses prior runs.)
6. **File -> Export -> Export Hardware**, overwrite the same `.xsa`.
7. In Vitis, open the platform's `vitis-comp.json`, click
   **Switch / re-read XSA** and re-pick the same `.xsa`.
8. Clean-build the application (broom icon under Flow).

## Diagram - the finished design at a glance

```
                           +------------------+
                           |  ZYNQ7 PS        |
            FCLK_CLK0  --> |                  |
            FIXED_IO  ---> |   ARM A9 + DDR   |
            DDR       ---> |                  |
                           +-+-------+-----+--+
                             |       |     |
                             | M_AXI |     |
                             | _GP0  |     |
                             v       |     |
                  +---------+-+      |     |
                  | AXI       |      |     |
                  | Intercon  |      |     |
                  +-+---+---+-+      |     |
                    |   |   |        |     |
        s_axi/Reg | s | s | s        |     |
                  v   v   v          |     |
            +-----+ +-+-+ +------+   |     |
            |GPIO0| |HLS| | HDMI |   |     |
            |LEDs | |top| | (VDMA|   |     |
            |Btns | |lev| |+ VTC |   |     |
            +-----+ |el | |+ rgb |   |     |
                    |   | | 2dvi |   |     |
                    |m  | |+ clk |   |     |
                    |a  | |  wiz |   |     |
                    |x  | +---+--+   |     |
                    |i  |     | m    |     |
                    v   |     v axi  |     |
                 +------+--+  +-----++     |
                 | AXI Int |  | AXI |      |
                 +----+----+  | SC  |      |
                      |       +--+--+      |
                      v          |         |
                  S_AXI_HP0      v         |
                              S_AXI_HP1    |
                                           v
                                        FIXED_IO
                                        (UART, Ethernet,
                                         USB, SD, etc.)
```

That's the whole hardware story. Everything else lives in software.
