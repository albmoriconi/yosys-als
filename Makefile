yosys-config = /yosys/yosys-config
yosys-exec-command = $(yosys-config) --exec --cxx --cxxflags -c
yosys-build-command = $(yosys-config) --build
als_dir = passes/als
boolector_include = -I/boolector/src
link_boolector = -L/boolector/build/lib -lboolector
eigen_include = -I/usr/include/eigen3
yosys_include = -I/yosys
opt = -O0 $(eigen_include) $(boolector_include) $(yosys_include)
obj_dir = obj
lib_dir = lib

all: makedirs $(lib_dir)/als.so

makedirs :
	mkdir -p $(obj_dir) $(lib_dir)

$(obj_dir)/smtsynth.o: $(als_dir)/smtsynth.cc
	 $(yosys-exec-command) $< -o $@ $(opt)

$(obj_dir)/smt_utils.o: $(als_dir)/smt_utils.cc
	$(yosys-exec-command) $< -o $@ $(opt)

$(obj_dir)/yosys_utils.o: $(als_dir)/yosys_utils.cc
	$(yosys-exec-command) $< -o $@ $(opt)

$(obj_dir)/als.o: $(als_dir)/als.cc
	$(yosys-exec-command) $< -o $@ $(eigen_include) $(opt)

$(obj_dir)/graph.o: $(als_dir)/graph.cc
	$(yosys-exec-command) $< -o $@ $(opt)

$(obj_dir)/Optimizer.o: $(als_dir)/Optimizer.cc
	$(yosys-exec-command) $< -o $@ $(opt)

$(lib_dir)/als.so: $(obj_dir)/smtsynth.o $(obj_dir)/smt_utils.o $(obj_dir)/yosys_utils.o $(obj_dir)/als.o $(obj_dir)/graph.o $(obj_dir)/Optimizer.o
	$(yosys-build-command) $@ $^ $(link_boolector)

clean:
	rm -rf $(lib_dir)/*.so $(obj_dir)/*.dSYM $(obj_dir)/*.d $(obj_dir)/*.o $(obj_dir)/*.tmp

.PHONY: clean all makedirs
