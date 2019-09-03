yosys-config = /usr/local/bin/yosys-config
yosys-exec-command = $(yosys-config) --exec --cxx --cxxflags -c
yosys-build-command = $(yosys-config) --build
als_dir = passes/als
eigen_include = -I/usr/local/include/eigen3
libs = -lboolector
opt = -O0

all: als.so

smtsynth.o: $(als_dir)/smtsynth.cc $(als_dir)/smtsynth.h $(als_dir)/smt_utils.h
	 $(yosys-exec-command) $< -o $@ $(opt)

smt_utils.o: $(als_dir)/smt_utils.cc $(als_dir)/smt_utils.h
	$(yosys-exec-command) $< -o $@ $(opt)

yosys_utils.o: $(als_dir)/yosys_utils.cc $(als_dir)/yosys_utils.h $(als_dir)/smtsynth.h
	$(yosys-exec-command) $< -o $@ $(opt)

als.o: $(als_dir)/als.cc $(als_dir)/smtsynth.h $(als_dir)/graph.h $(als_dir)/optimizer.h $(als_dir)/yosys_utils.h $(als_dir)/smt_utils.h
	$(yosys-exec-command) $< -o $@ $(opt)

graph.o: $(als_dir)/graph.cc $(als_dir)/graph.h
	$(yosys-exec-command) $< -o $@ $(opt)

optimizer.o: $(als_dir)/optimizer.cc $(als_dir)/optimizer.h $(als_dir)/smtsynth.h $(als_dir)/smt_utils.h $(als_dir)/graph.h
	$(yosys-exec-command) $< -o $@ $(eigen_include) $(opt)

als.so: smtsynth.o smt_utils.o yosys_utils.o als.o graph.o optimizer.o
	$(yosys-build-command) $@ $^ $(libs) $(opt)

clean:
	rm -rf *.so *.dSYM *.d *.o

.PHONY: clean
