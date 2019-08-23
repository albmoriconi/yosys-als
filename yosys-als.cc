/* -*- c++ -*-
 *  yosys-als -- Approximate logic synthesis
 *
 *  Copyright (C) 2019  Alberto Moriconi <a.moriconi@studenti.unina.it>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "smt-synthesis.h"
#include "kernel/yosys.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct AlsWorker {
	struct and_inv_ports {
		Wire *in1;
		Wire *in2;
		Wire *outp1;
		Wire *outp0;
	};

	bool debug = false;
	dict<IdString, smt::aig_model_t> synthesized_luts;

	void synthesize_lut(const Cell *const cell, const int ax_degree) {
		if (debug) {
			log("\n=== %s ===\n\n", cell->name.c_str());
			log("   LUT function: %s\n\n", cell->getParam("\\LUT").as_string().c_str());
		}

		smt::aig_model_t aig = smt::lut_synthesis(cell->getParam("\\LUT").as_string(), ax_degree);

		if (debug) {
			for (int i = 0; i < GetSize(aig.s); i++) {
				log("      %d\t", i);
				if (i == 0)
					log("C0\n");
				else if (i < aig.num_inputs)
					log("PI\n");
				else
					log("AND(%dp%d, %dp%d)\n", aig.s[i].first, aig.p[i].first, aig.s[i].second, aig.p[i].second);
			}
		}

		synthesized_luts[cell->name] = aig;
	}

	void run(Module *top_mod) {
		// 1. 4-LUT synthesis
		ScriptPass::call(top_mod->design, "synth -lut 4");
		log("\n");

		// 2. SMT exact synthesis
		log_header(top_mod->design, "Running SMT exact synthesis for LUTs\n");
		for (auto cell : top_mod->cells()) {
			if (cell->hasParam("\\LUT"))
				synthesize_lut(cell, 0);
		}
		if (debug)
			log("\n");

		// 3. Update design
		// TODO Handle constant case
		/*
		for (auto &lut : synthesized_luts) {
		    // Save LUT ins and outs
		    SigSpec lut_in;
		    SigSpec lut_out;
		    for (auto &conn : top_mod->cell(lut.first)->connections()) {
			if (top_mod->cell(lut.first)->input(conn.first))
			    lut_in = conn.second;
			else if (top_mod->cell(lut.first)->output(conn.first))
			    lut_out = conn.second;
		    }

		    // Prepare negate LUT ins
		    SigSpec lut_in_p0(lut_in.size());
		    top_mod->addNot(NEW_ID, lut_in, lut_in_p0);

		    // Create AND gates
		    std::vector<and_inv_ports> cell_ports;
		    for (size_t i = 0; i < lut.second.s.size(); i++) {
			Wire *in1 = top_mod->addWire(NEW_ID);
			Wire *in2 = top_mod->addWire(NEW_ID);
			Wire *outp1 = top_mod->addWire(NEW_ID);
			Wire *outp0 = top_mod->addWire(NEW_ID);
			top_mod->addAndGate(NEW_ID, in1, in2, outp1);
			top_mod->addNotGate(NEW_ID, outp1, outp0);
			cell_ports.push_back(and_inv_ports {in1, in2, outp1, outp0});
		    }

		    // Create connections
		    for (size_t i = 0; i < lut.second.s.size(); i++) {
			auto curr_ins = lut.second.s[i];
			auto curr_ins_p = lut.second.p[i];

			if (curr_ins.first < lut_in.size() + 1) {
			    if (curr_ins_p.first)
				top_mod->connect(cell_ports[i].in1, lut_in[curr_ins.first - 1]);
			    else
				top_mod->connect(cell_ports[i].in1, lut_in_p0[curr_ins.first - 1]);
			} else {
			    if (curr_ins_p.first)
				top_mod->connect(cell_ports[i].in1, cell_ports[curr_ins.first - lut_in.size() - 1].outp1);
			    else
				top_mod->connect(cell_ports[i].in1, cell_ports[curr_ins.first - lut_in.size() - 1].outp0);
			}

			if (curr_ins.second < lut_in.size() + 1) {
			    if (curr_ins_p.second)
				top_mod->connect(cell_ports[i].in2, lut_in[curr_ins.second - 1]);
			    else
				top_mod->connect(cell_ports[i].in2, lut_in_p0[curr_ins.second - 1]);
			} else {
			    if (curr_ins_p.second)
				top_mod->connect(cell_ports[i].in2, cell_ports[curr_ins.second - lut_in.size() - 1].outp1);
			    else
				top_mod->connect(cell_ports[i].in2, cell_ports[curr_ins.second - lut_in.size() - 1].outp0);
			}
		    }
		    if (lut.second.out_p)
			top_mod->connect(lut_out, cell_ports.back().outp1);
		    else
			top_mod->connect(lut_out, cell_ports.back().outp0);

		    // Delete LUT
		    // TODO Can we do this better?
		    top_mod->remove(top_mod->cell(lut.first));
		}
		 */

		// 4. Clean and reduce
		Pass::call(top_mod->design, "opt_clean");
		//Pass::call(design, "abc -script +strash;ifraig;dc2");
		Pass::call(top_mod->design, "freduce");

		// 5. Print stats
		Pass::call(top_mod->design, "stat");
	}
};

struct AlsPass : public Pass {
	AlsPass() : Pass("als", "approximate logic synthesis") {}

	void help() YS_OVERRIDE {
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    als [options] [top-level]\n");
		log("\n");
		log("This command executes an approximate logic synthesis.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) YS_OVERRIDE {
		AlsWorker worker;

		log_header(design, "Executing ALS pass (approximate logic synthesis).\n");
		log_push();

		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++) {
			if (args[argidx] == "-d") {
				worker.debug = true;
				continue;
			}
		}
		extra_args(args, argidx, design);

		Module *top_mod = nullptr;

		if (design->full_selection()) {
			top_mod = design->top_module();

			if (!top_mod)
				log_cmd_error("Design has no top module, use the 'hierarchy' command to specify one.\n");
		} else {
			auto mods = design->selected_whole_modules();
			if (GetSize(mods) != 1)
				log_cmd_error("Only one top module must be selected.\n");
			top_mod = mods.front();
		}

		worker.run(top_mod);

		log_pop();
	}
} AlsPass;

PRIVATE_NAMESPACE_END
