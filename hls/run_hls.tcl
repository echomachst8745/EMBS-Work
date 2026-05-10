# =============================================================================
# run_hls.tcl
#
# Headless Vitis HLS 2025.1 build script for the Nonogram line accelerator.
# Source this from a Vitis HLS Tcl console (or `vitis_hls -i run_hls.tcl`).
# Produces an exported IP catalog at:
#
#   ./nonogram_line_hls/solution1/impl/ip
#
# The Vivado script `vivado/create_project.tcl` looks for the IP repo at
# the directory containing this script (i.e. ../hls), so by default it
# will find the exported core.
# =============================================================================

# Project name (also IP catalog directory name)
open_project -reset nonogram_line_hls

# Top-level function (so the generated driver is XToplevel_*)
set_top toplevel

# Sources
add_files nonogram_line_accel.cpp
add_files nonogram_line_accel.h
add_files -tb testbench.cpp

# Solution
open_solution -reset solution1
# Zybo Z7-10 part (matches Vivado project)
set_part {xc7z010clg400-1}

# 100 MHz target clock (10 ns) - matches the rest of the EMBS practicals.
create_clock -period 10 -name default

# Run C simulation first (fail-fast on logic errors)
csim_design

# Synthesise
csynth_design

# Optional: co-sim (slow). Uncomment if you want to verify post-synthesis.
# cosim_design -trace_level all

# Export as a Vivado IP catalog repo so the create_project.tcl picks it up.
export_design -format ip_catalog -display_name "Nonogram Line Accelerator" \
                                 -description "EMBS Part III HLS line propagator." \
                                 -vendor "user.org" -library "hls" -version "1.0"

puts ""
puts "=========================================================="
puts " HLS done. Add this directory to Vivado IP repository:"
puts "   [pwd]"
puts " (create_project.tcl already does this if hls_ip_repo_dir is set.)"
puts "=========================================================="

# In a non-interactive run, exit cleanly. In an interactive console this
# is a no-op.
exit
