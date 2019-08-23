yosys-config = /usr/local/bin/yosys-config
srcs = yosys-als.cc smt-synthesis.cc
libs = -lz3

yosys-als.so: $(srcs)
	$(yosys-config) --build $@ $^ $(libs)

clean:
	rm -rf *.so *.dSYM *.d

.PHONY: clean
