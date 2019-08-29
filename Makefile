yosys-config = /usr/local/bin/yosys-config
srcs = als.cc smtsynth.cc axmiter.cc utils.cc yosys_utils.cc
libs = -lz3

als.so: $(srcs)
	$(yosys-config) --build $@ $^ $(libs)

clean:
	rm -rf *.so *.dSYM *.d

.PHONY: clean
