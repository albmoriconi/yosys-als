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

#include "smtsynth.h"
#include "graph_utils.h"
#include "yosys_utils.h"
#include "kernel/yosys.h"

#include <string>
#include <vector>

USING_YOSYS_NAMESPACE

/**
 * @brief Yosys ALS module namespace
 */
namespace yosys_als {

    /**
     * @brief Worker for the ALS pass
     */
    struct AlsWorker {
        /// If \c true, log debug information
        bool debug = false;

        /// Index of the synthesized LUTs
        dict<Const, std::vector<mig_model_t>> synthesized_luts;

        /**
         * Runs an ALS step on selected module
         * @param module A module
         */
        void run(Module *const module) {
            // 1. 4-LUT synthesis
            ScriptPass::call(module->design, "synth -lut 4");

            // 2. SMT exact synthesis
            log_header(module->design, "Running SMT exact synthesis for LUTs.\n");
            for (auto cell : module->cells()) {
                if (is_lut(cell)) {
                    const auto &fun_spec = get_lut_param(cell);

                    if (synthesized_luts.find(fun_spec) == synthesized_luts.end()) {
                        synthesized_luts[fun_spec] = std::vector<mig_model_t>{synthesize_lut(fun_spec, 0, debug)};

                        // TODO Add multiple approximate candidates
                        //if (synthesized_luts[fun_spec].num_gates > 0) {
                        //    auto approximate_candidate = synthesize_lut(fun_spec, 1, debug);
                        //    if (approximate_candidate.num_gates < synthesized_luts[fun_spec].num_gates)
                        //        approximated_luts[fun_spec].push_back(std::move(approximate_candidate));
                        //}
                    }
                }
            }

            // 3. Create a graph structure
            Graph g = graph_from_module(module);

            // ...
        }
    };

    /**
     * \brief Yosys ALS pass
     */
    struct AlsPass : public Pass {
        AlsPass() : Pass("als", "approximate logic synthesis") {}

        void help() YS_OVERRIDE {
            //   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
            log("\n");
            log("    als [options] [selection]\n");
            log("\n");
            log("This command executes an approximate logic synthesis.\n");
            log("\n");
            log("    -d\n");
            log("        enable debug output\n");
            log("\n");
        }

        void execute(std::vector<std::string> args, Design *design) YS_OVERRIDE {
            log_header(design, "Executing ALS pass (approximate logic synthesis).\n");
            log_push();

            AlsWorker worker;

            // TODO Add arguments for specifying input probability
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
} // namespace yosys_als
