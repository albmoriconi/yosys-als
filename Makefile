yosys-config = /usr/local/bin/yosys-config
srcs = als.cc smtsynth.cc axmiter.cc
libs = -lz3

als.so: $(srcs)
	$(yosys-config) --build $@ $^ $(libs)

clean:
	rm -rf *.so *.dSYM *.d

.PHONY: clean
