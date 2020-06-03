#!/usr/bin/env bash

# Please set your Vivado installation path
vivado_path="/tools/Xilinx/Vivado/2019.2"

# Exit on error
set -e

# Parse arguments
if [ -z $2 ]; then
  basepath=${1##*/}
  prefix=${basepath%.*}
  set $1 $prefix
fi 

# Set Vivado environment variables
source ${vivado_path}/settings64.sh

# Run scripts
yosys -p "tcl als.tcl $1 $2"
yosys -p "tcl synth_variants.tcl $2"
vivado -mode batch -source report_power.tcl -tclargs als_${2}_xilinx
