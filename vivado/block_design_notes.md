# Vivado Block Design Notes

This describes the block design that `create_project.tcl` builds, so that
if you build it manually (e.g. through the GUI) you can reproduce the
same design. It targets the Zybo Z7-10 (`xc7z010clg400-1`) on Vivado
2025.1.

## Required IP repositories

In `Settings → IP → Repository`, add **two** local paths:

1. The EMBS HDMI repo: `<...>/zybo-z7-hdmi/hardware/zybo_z7_hdmi_repo`
   ([source](https://github.com/RTSYork/zybo-z7-hdmi)). This is the only
   third-party repo allowed by the EMBS docs (it provides the Digilent
   RGB2DVI core that the HDMI page tells us to use).
2. Your built HLS IP repo: the directory of the HLS project that
   contains the exported `toplevel` core (after running
   `Package → output.format = ip_catalog`).

## Block diagram contents

| Block | Notes |
| --- | --- |
| `processing_system7_0` (ZYNQ7) | Configured from the Zybo Z7-10 board preset. |
| HP0 (S_AXI_HP0) | Enabled. Master = HLS `toplevel_0/m_axi_MAXI`. |
| HP1 (S_AXI_HP1) | Enabled. Master = the HDMI VDMA inside the HDMI hierarchy. |
| GP0 (M_AXI_GP0) | Master for AXI-Lite slaves (HLS, GPIO, VTC, VDMA control). |
| TTC0 / Timer 0 | Enabled, EMIO (so `freertos_ps7_cortexa9_0` can use it). |
| `axi_gpio_0` | Channel 1 = LEDs (4 bits, output), Channel 2 = Buttons (4 bits, input). Used as a "heartbeat" for the demo. |
| HDMI hierarchy | Created by sourcing `<...>/zybo_z7_hdmi.tcl` after the PS exists. Contains AXI VDMA + VTC + Digilent rgb2dvi + clocking. |
| `toplevel_0` | Our custom HLS IP. AXI-Lite slave on AXILiteS, AXI master on MAXI. |

## Connection rules

* `processing_system7_0/M_AXI_GP0` → connection automation → all AXI-Lite
  slaves (HLS, AXI GPIO, VDMA, VTC).
* `toplevel_0/m_axi_MAXI` → connection automation → `processing_system7_0/S_AXI_HP0`.
* HDMI VDMA AXI master (`Data_S2MM`/`MM2S` depending on whether you are
  reading or writing the framebuffer) → `processing_system7_0/S_AXI_HP1`.
* External ports created by the HDMI TCL: `hdmi_clk_n/p`, `hdmi_d_n/p`,
  `hdmi_oen`, `hdmi_scl/sda`. The `zybo_z7_hdmi.xdc` constraint file maps
  these to the correct package pins.

## Constraints

Add `<...>/zybo-z7-hdmi/hardware/zybo_z7_hdmi.xdc` to `constrs_1`. This
maps the HDMI external ports to the Zybo Z7's HDMI TX pins. Make sure
you tick "Copy constraint files into project" when adding it.

## Address map

When validate_bd_design / address editor runs you should see (defaults
are fine):

| Slave | Range |
| --- | --- |
| `toplevel_0/s_axi_AXILiteS/Reg` | 0x43C00000 – 0x43C0FFFF |
| `axi_gpio_0/S_AXI/Reg` | 0x41200000 – 0x4120FFFF |
| HDMI VDMA / VTC | 0x43000000 onward (the EMBS HDMI capture daemon assumes 0x43000000 for VDMA) |

If anything is "Unmapped" in the address editor, click the auto-assign
button.

## Build

Once the design is validated:

1. `Generate Output Products` → Global → OK.
2. `Generate Bitstream` (this is slow).
3. `File → Export → Export Hardware → Include bitstream → Finish`. Save
   as `design_1_wrapper.xsa` next to the project. This is what we
   import into Vitis as a Platform.

## Re-building after HLS changes

When you change the HLS code, follow the EMBS knowledge base recipe:

1. Re-run synthesis in HLS, re-export as IP catalog (same path).
2. Back in Vivado: `IP Status → Re-run Report → Refresh IP Catalog`.
3. Click `Upgrade Selected` on the `toplevel` IP entry.
4. Generate Bitstream, Export Hardware (overwrite the `.xsa`).
5. In Vitis, `vitis-comp.json → Switch / re-read XSA`.
