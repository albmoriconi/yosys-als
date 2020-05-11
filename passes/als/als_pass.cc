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

// TODO Add LICENSE
// TODO Add README
/**
 * @file
 * @brief Approximate logic synthesis pass for Yosys ALS module
 */

#include "als_pass.h"
#include "als_worker.h"
#include <string>
#include <vector>

USING_YOSYS_NAMESPACE



void yosys_als::AlsPass::help() {
	//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
	log("\n");
	log("    als [options] [selection]\n");
	log("\n");
	log("This command executes an approximate logic synthesis.\n");
	log("\n");
	log("    -w <signal> <value>\n");
	log("        set the weight for the output signal to the specified power of two.\n");
	log("\n");
	log("\n");
	log("    -d\n");
	log("        enable debug output\n");
	log("\n");
}

/**
 * @brief This is the plug-in entry point.
 * 
 * @param args command-line args given to the Yosys tool
 * @param design desing on which the plugin will operate Approximate-Logic Synthesis
 */
void yosys_als::AlsPass::execute(std::vector<std::string> args, Design *design) {
	log_header(design, "Executing ALS pass (approximate logic synthesis).\n");
	log_push();

	AlsWorker worker;
	std::vector<std::pair<std::string, std::string>> weights;

	// TODO Add arguments for specifying input probability
	size_t argidx;
	for (argidx = 1; argidx < args.size(); argidx++) {
		if (args[argidx] == "-w" && argidx + 2 < args.size()) {
			std::string lhs = args[++argidx].c_str();
			std::string rhs = args[++argidx].c_str();
			weights.emplace_back(lhs, rhs);
			continue;
		}
		if (args[argidx] == "-d") {
			worker.debug = true;
			continue;
		}
	}
	extra_args(args, argidx, design);

	Module *top_mod = nullptr;

	if (design->full_selection())
	{
		top_mod = design->top_module();
		if (!top_mod)
			log_cmd_error("Design has no top module, use the 'hierarchy' command to specify one.\n");
	}
	else
	{
		auto mods = design->selected_whole_modules();
		if (GetSize(mods) != 1)
			log_cmd_error("Only one top module must be selected.\n");
		top_mod = mods.front();
	}

	// TODO gather signal weights
//	for (auto &w : weights) {
//		RTLIL::SigSpec lhs;
//		if (!RTLIL::SigSpec::parse_sel(lhs, design, top_mod, w.first))
//			log_cmd_error("Failed to parse lhs weight expression `%s'.\n", w.first.c_str());
//		if (!lhs.is_wire() || !lhs.as_wire()->port_output)
//			log_cmd_error("Lhs weight expression `%s' not an output.\n", w.first.c_str());
//
//		worker.weights[lhs] = std::stod(w.second);
//	}

    // TODO gather catalog file name and report file name

	worker.run(top_mod);

	log_pop();
}
