yosys-config = /usr/local/bin/yosys-config
als_dir = passes/als
srcs = $(als_dir)/als.cc $(als_dir)/smtsynth.cc $(als_dir)/utils.cc $(als_dir)/yosys_utils.cc $(als_dir)/graph_utils.cc
libs = -lboolector

als.so: $(srcs)
	$(yosys-config) --build $@ $^ $(libs) -O0

clean:
	rm -rf *.so *.dSYM *.d

.PHONY: clean
