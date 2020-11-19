#!/usr/bin/env bash

# Exit on error
set -e

# Parse arguments
if [ -z $2 ]; then
  basepath=${1##*/}
  prefix=${basepath%.*}
  set $1 $prefix
fi 

# Run scripts
yosys -p "tcl als.tcl $1 $2"
yosys -p "tcl synth_variants.tcl $2"
vivado -mode batch -source report_xilinx.tcl -tclargs als_${2}_xilinx
