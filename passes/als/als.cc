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

#include "../../utils/smtsynth.h"
#include "../../utils/utils.h"
#include "../../utils/yosys_utils.h"
#include "kernel/yosys.h"

#include <queue>
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

        /// If \c true, the worker is executing its first step
        bool first_step = true;

        /// If \c true, the worker found a positive reward in last step
        bool done_something = false;

        /// Keep a copy of the golden module
        Module *golden = nullptr;

        /// Index of the synthesized LUTs (kept between steps)
        dict<Const, aig_model_t> synthesized_luts;

        /// Index of the approximately synthesized LUTs (kept between steps)
        dict<Const, aig_model_t> approximated_luts;

        /// The threshold for the approximation miter
        int threshold = 0;

        /// A queue of the possible LUT-to-AIG mappings
        std::queue<dict<IdString, aig_model_t>> lut_to_aig_mapping_queue;

        /// Identifier of the golden module
        IdString golden_id;

        // TODO Document method
        // TODO We can do better than take a pointer for ax_cell
        // Alternatives:
        //  - A variant type
        //  - A vector
        void push_substitution_index(Module *const module, Cell *const ax_cell = nullptr) {
            dict<IdString, aig_model_t> substitution_index;

            if (ax_cell != nullptr)
                substitution_index[ax_cell->name] = approximated_luts[get_lut_param(ax_cell)];

            for (auto cell : module->cells()) {
                if (cell != ax_cell && is_lut(cell)) {
                    auto lut_it = synthesized_luts.find(get_lut_param(cell));

                    if (lut_it != synthesized_luts.end())
                        substitution_index[cell->name] = lut_it->second;
                }
            }

            lut_to_aig_mapping_queue.push(substitution_index);
        }

        // TODO Document method
        // TODO Refactor method
        void replace_approximated_greedy(Module *module) {
            log_header(module->design, "Finding best approximate LUT substitution.\n");
            log_push();

            // 0. Some book keeping
            IdString top_mod_id = module->name;
            IdString working_id = RTLIL::escape_id("als_working");
            IdString axmiter_id = RTLIL::escape_id("als_axmiter");
            Module *working = cloneInDesign(module, working_id, module->design, debug);

            // 1. Evaluate baseline
            log("Evaluating baseline...\n");
            push_substitution_index(working);
            apply_mapping(working, lut_to_aig_mapping_queue.front(), debug);
            clean_and_freduce(working);
            lut_to_aig_mapping_queue.pop();
            int and_cells_baseline = count_cells(working, "$_AND_", debug);
            working = cloneInDesign(module, working_id, module->design, debug);

            // 2. Evaluate alternatives
            for (auto cell : working->cells()) {
                if (is_lut(cell)) {
                    auto ax_lut_it = approximated_luts.find(get_lut_param(cell));

                    if (ax_lut_it != approximated_luts.end()) {
                        push_substitution_index(working, cell);

                        if (debug)
                            log("Alternative with approximate variant of %s\n", cell->name.c_str());
                    }
                }
            }

            int best_reward = 0;
            dict<IdString, aig_model_t> best_substitution;
            while (!lut_to_aig_mapping_queue.empty()) {
                apply_mapping(working, lut_to_aig_mapping_queue.front(), debug);
                clean_and_freduce(working);

                int and_cells_candidate = count_cells(working, "$_AND_", debug);
                int reward = and_cells_baseline - and_cells_candidate;
                if (debug)
                    log("Current step reward: %d\n", reward);

                if (reward > best_reward) {
                    Pass::call(working->design, vformat("axmiter -threshold %d %s %s %s", threshold,
                            unescape_id(golden_id).c_str(), unescape_id(working_id).c_str(), unescape_id(axmiter_id).c_str()));
                    Pass::call_on_module(working->design, working->design->modules_[axmiter_id], "flatten");
                    Pass::call(working->design, vformat("als_sat -prove trigger 0 %s", unescape_id(axmiter_id).c_str()));

                    if (working->design->scratchpad_get_bool("als_sat_success")) {
                        best_reward = reward;
                        best_substitution = lut_to_aig_mapping_queue.front();
                        done_something = true;
                        if (debug)
                            log("Found an improvement.\n");
                    }
                    Pass::call(working->design, vformat("delete %s", unescape_id(axmiter_id).c_str()));
                }
                if (debug)
                    log("\n");

                // Restore design
                working = cloneInDesign(module, working_id, module->design, debug);
                lut_to_aig_mapping_queue.pop();
            }

            // Restore best
            log("Best reward: %d\n", best_reward);
            if (done_something)
                lut_to_aig_mapping_queue.push(best_substitution);
            else
                push_substitution_index(module);

            apply_mapping(module, lut_to_aig_mapping_queue.front(), debug);
            clean_and_freduce(module);
            lut_to_aig_mapping_queue.pop();
            log_pop();
        }

        /**
         * Runs an ALS step on selected module
         * @param module A module
         */
        void run_step(Module *module) {
            // 0. Some book keeping
            done_something = false;

            if (first_step) {
                golden_id = RTLIL::escape_id("als_golden");
                golden = cloneInDesign(module, golden_id, nullptr, debug);
                first_step = false;
            }

            // 1. 4-LUT synthesis
            ScriptPass::call(module->design, "synth -lut 4");
            cloneInDesign(golden, golden_id, module->design, debug);

            // 2. SMT exact synthesis
            log_header(module->design, "Running SMT exact synthesis for LUTs.\n");
            for (auto cell : module->cells()) {
                if (is_lut(cell)) {
                    const auto &fun_spec = get_lut_param(cell);

                    if (synthesized_luts.find(fun_spec) == synthesized_luts.end()) {
                        synthesized_luts[fun_spec] = synthesize_lut(fun_spec, 0, debug);

                        // TODO Add multiple approximate candidates (e.g. 1, 2, 4)
                        if (synthesized_luts[fun_spec].num_gates > 0) {
                            approximated_luts[fun_spec] = synthesize_lut(fun_spec, 1, debug);
                            if (approximated_luts[fun_spec].num_gates >= synthesized_luts[fun_spec].num_gates)
                                approximated_luts.erase(fun_spec);
                        }
                    }
                }
            }

            // 3. Replace greedily
            replace_approximated_greedy(module);
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

            // TODO Add arguments for specifying output format
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

            worker.threshold = 5;
            while (worker.first_step || worker.done_something)
                worker.run_step(top_mod);

            log_pop();
        }
    } AlsPass;
} // namespace yosys_als
