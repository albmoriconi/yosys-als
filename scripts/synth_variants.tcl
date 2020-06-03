# Check and parse arguments
if { $argc < 1 } {
    puts "Please specify module name."
    exit 1
}

set module_name [lindex $argv 0]

# Save results
file mkdir als_${module_name}_xilinx
foreach file [glob -directory als_${module_name}_replaced *.v] {
    yosys read_verilog $file
    yosys synth_xilinx
    yosys write_edif als_${module_name}_xilinx/[file tail [file rootname $file]].edif
    yosys delete
}
