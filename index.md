### What is yosys-als?

yosys-als is a design space exploration tool for approximate circuits, implemented as a plugin for the [Yosys Open Synthesis Suite](http://www.clifford.at/yosys/).

### How can I install it?

The [project repository](https://github.com/albmoriconi/yosys-als) provides a Dockerfile that you can use to get started quickly.
If you prefer to run it in your environment of choice, you'll need:

- [Yosys 0.9](https://github.com/YosysHQ/yosys)
- [Boolector 3.2.1](https://github.com/boolector/boolector/)

In addition to the dependencies of these two softwares, you'll also need these additional packages:

- libboost-serialization-dev
- libboost-graph-dev
- libboost-dev
- libsqlite3-dev

Then you can compile yosys-als running Cmake:

```
$ git clone https://github.com/albmoriconi/yosys-als.git
$ cd yosys-als
$ cmake -B build
$ cmake --build build
```

It can be handy to copy yosys-als to the Yosys plugin directory:

```
$ sudo cmake --build build --target install_plugin
```

### How can I try it?

yosys-als is distributed with some example scripts that you can use to search for approximate variants of a combinatorial circuit, to synthesize them and to run a power estimation using the [Xilinx Vivado Design Suite](https://www.xilinx.com/products/design-tools/vivado.html).

If you just want to give it a try, start with a Verilog or SystemVerilog description of a simple circuit (VHDL is also supported, but requires the [GHDL Yosys plugin](https://github.com/ghdl/ghdl-yosys-plugin)); e.g., you can save the following to `mult_2_bit.sv`:

```systemverilog
// 2-bit multiplier

module mult_2_bit (
    input  logic [1:0]  a,
    output logic [3:0]  o,
    input  logic [1:0]  b
);

  logic and01;
  logic and10;
  logic and11;
  logic and0110;

  assign and01 = a[0] & b[1];
  assign and10 = a[1] & b[0];
  assign and11 = a[1] & b[1];
  assign and0110 = and01 & and10;

  assign o[0] = a[0] & b[0];
  assign o[1] = and01 ^ and10;
  assign o[2] = and11 ^ and0110;
  assign o[3] = and11 & and0110;

endmodule
```

Launch Yosys and insert the commands:

```
yosys> plugin -i als
yosys> read_verilog -sv mult_2_bit.sv 
yosys> splitnets -ports
yosys> als
```

You'll obtain a table with a short report of the results, e.g.:

```
 Entry     Chosen LUTs         Arel        Gates
 ----- --------------- ------------ ------------
     0            0000            0            1
     1            1110       0.0625     0.416667
[...]
     5            1250         0.25         0.25
[...]
```

The resulting variants will also be saved in the Yosys `ilang` format to the `als_mult_2_bit` directory; the `synt_variants.tcl` script can be used to read, synthesize and write them back in Verilog.

### How does it work?

_This section is being updated. Please come back soon!_
