yosys-config = /usr/local/bin/yosys-config
als_dir = passes/als
sat_dir = passes/sat
utils_dir = utils
srcs_passes = $(als_dir)/als.cc $(als_dir)/axmiter.cc $(sat_dir)/als_sat.cc
srcs_utils = $(utils_dir)/smtsynth.cc $(utils_dir)/utils.cc $(utils_dir)/yosys_utils.cc
srcs = $(srcs_passes) $(srcs_utils)
libs = -lz3

als.so: $(srcs)
	$(yosys-config) --build $@ $^ $(libs)

clean:
	rm -rf *.so *.dSYM *.d

.PHONY: clean
