# Check and parse arguments
if { $argc < 1 } {
    puts "Please specify EDIF directory."
    exit 1
}

set source_dir [lindex $argv 0]

foreach file [glob -directory $source_dir *.edif] {
    # Load Edif and select part
    set_property design_mode GateLvl [current_fileset]
    add_files -norecurse $file
    set_property top_file $file [current_fileset]
    link_design -name netlist_1 -part xc7a35ticsg324-1L

    # Create a virtual clock and run power analysis
    create_clock -name clk_virt -period 1
    report_power -file ${source_dir}/[file tail [file rootname $file]]_report.txt

    # Close project
    close_project
}
