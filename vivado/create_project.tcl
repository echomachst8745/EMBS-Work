# =============================================================================
# create_project.tcl
#
# Reproducible Vivado 2025.1 project creation script for the EMBS Part III
# Nonogram solver. Run from a Vivado tcl console with:
#
#   cd <somewhere outside the EMBS repo>
#   source <path>/EMBS_Part3_Nonogram_Solver/vivado/create_project.tcl
#
# Before sourcing, edit the three "USER CONFIG" variables below to point
# at the local clones of:
#   * The EMBS-provided HDMI repo (zybo-z7-hdmi)
#   * Your built HLS IP repo (the directory of the HLS project that
#     contains the exported "toplevel" core)
#
# This script automates the steps that practicals 1a, 4, 5 and 6 walk
# through manually, and matches the layout described in
# vivado/block_design_notes.md.
# =============================================================================

# ----------------------------- USER CONFIG -----------------------------------
# Where to create the new Vivado project (must NOT contain spaces)
set proj_dir          [pwd]/embs_nonogram_vivado
set proj_name         embs_nonogram

# Path to the EMBS HDMI repo (contains hardware/zybo_z7_hdmi.tcl etc.)
# See: https://iangray001.github.io/embs/docs/resourcestips/hdmi-output/
set hdmi_repo_dir     "$::env(HOME)/embs/zybo-z7-hdmi"

# Path to the directory which contains your HLS project's solution1/impl/
# i.e. the directory that Vivado's "Add IP Repository" needs.  This is
# the parent of the HLS project, not the .cpp source directory.
set hls_ip_repo_dir   "$::env(HOME)/embs/EMBS_Part3_Nonogram_Solver/hls"
# -----------------------------------------------------------------------------

# Use the Zybo Z7-10 board files (digilentinc.com)
set fpga_part         "xc7z010clg400-1"
set board_part        "digilentinc.com:zybo-z7-10:part0:1.1"

create_project $proj_name $proj_dir -part $fpga_part
set_property board_part $board_part [current_project]

# Add the HDMI Digilent IP repo + your HLS IP repo
set_property ip_repo_paths [list \
    "$hdmi_repo_dir/hardware/zybo_z7_hdmi_repo" \
    "$hls_ip_repo_dir" \
] [current_project]
update_ip_catalog -rebuild

# Create a block design called design_1 (matches Vitis "design_1_wrapper")
create_bd_design "design_1"

# 1. Zynq Processing System ---------------------------------------------------
create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7 processing_system7_0
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 \
    -config { make_external "FIXED_IO, DDR" Master "Disable" Slave "Disable" } \
    [get_bd_cells processing_system7_0]

# Enable HP0 (HLS master) and HP1 (HDMI master), enable TTC0 for FreeRTOS,
# enable Timer 0 on EMIO (per Practical 5), and turn on UART1 + GEM0 + USB0 +
# SD0 which the standalone Zynq board preset gives us.
set_property -dict [list \
    CONFIG.PCW_USE_S_AXI_HP0 {1} \
    CONFIG.PCW_USE_S_AXI_HP1 {1} \
    CONFIG.PCW_TTC0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_TTC0_TTC0_IO {EMIO} \
] [get_bd_cells processing_system7_0]

# 2. Add the HDMI hierarchy via the EMBS-provided TCL ------------------------
# This adds the AXI VDMA, Xilinx VTC, Digilent rgb2dvi and clocking, plus
# external pins. It needs the PS to already exist (which it does by now).
source "$hdmi_repo_dir/hardware/zybo_z7_hdmi.tcl"

# 3. AXI GPIO for buttons + LEDs (status indicators) -------------------------
# These come from the Zybo board preset.
startgroup
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio axi_gpio_0
apply_board_connection -board_interface "leds_4bits" -ip_intf "axi_gpio_0/GPIO" -diagram "design_1"
apply_board_connection -board_interface "btns_4bits" -ip_intf "axi_gpio_0/GPIO2" -diagram "design_1"
endgroup

# 4. Add the custom HLS IP core ----------------------------------------------
create_bd_cell -type ip -vlnv user.org:hls:toplevel toplevel_0

# 5. Run connection automation - HLS slave, HLS master ----------------------
# Slave (axi_lite control) -> M_AXI_GP0
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config { Master "/processing_system7_0/M_AXI_GP0" Clk "Auto" } \
    [get_bd_intf_pins toplevel_0/s_axi_AXILiteS]
# Master (m_axi MAXI) -> S_AXI_HP0
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config { Master "/toplevel_0/m_axi_MAXI" Slave "/processing_system7_0/S_AXI_HP0" Clk "Auto" } \
    [get_bd_intf_pins processing_system7_0/S_AXI_HP0]
# AXI GPIO control link
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config { Master "/processing_system7_0/M_AXI_GP0" Clk "Auto" } \
    [get_bd_intf_pins axi_gpio_0/S_AXI]

# Note: the HDMI tcl script is expected to wire S_AXI_HP1 itself.

# 6. Validate, save, build, export -----------------------------------------
validate_bd_design
save_bd_design

# Add the HDMI XDC pin constraints
add_files -fileset constrs_1 -norecurse "$hdmi_repo_dir/hardware/zybo_z7_hdmi.xdc"

# Create the HDL wrapper
make_wrapper -files [get_files [get_property DIRECTORY [current_project]]/$proj_name.srcs/sources_1/bd/design_1/design_1.bd] -top
add_files -norecurse [get_property DIRECTORY [current_project]]/$proj_name.gen/sources_1/bd/design_1/hdl/design_1_wrapper.v

set_property top design_1_wrapper [current_fileset]
update_compile_order -fileset sources_1

puts ""
puts "============================================================"
puts " Project created. Next steps (manual):"
puts "   1. launch_runs synth_1 -jobs 4"
puts "   2. wait_on_run synth_1"
puts "   3. launch_runs impl_1 -to_step write_bitstream -jobs 4"
puts "   4. wait_on_run impl_1"
puts "   5. write_hw_platform -fixed -include_bit -force \\"
puts "        -file $proj_dir/design_1_wrapper.xsa"
puts " Or just hit 'Generate Bitstream' / File -> Export -> Hardware"
puts " in the GUI and choose to overwrite the .xsa each time."
puts "============================================================"
