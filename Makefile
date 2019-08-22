yosys-config = /usr/local/bin/yosys-config
srcs = yosys-als.cpp lut_synthesis.cpp

yosys-als.so: $(srcs)
	$(yosys-config) --build $@ $^ -lz3

clean:
	rm -rf *.so *.dSYM *.d

.PHONY: clean
