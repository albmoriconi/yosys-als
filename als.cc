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
// TODO Move to passes/als directory
/**
 * @file
 * @brief Approximate logic synthesis pass for Yosys ALS module
 */

#include "smtsynth.h"
#include "yosys_utils.h"
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

        /// A queue of the possible LUT-to-AIG mappings
        std::queue<dict<IdString, aig_model_t>> lut_to_aig_mapping_queue;

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

            // 1. Make a working copy
            Module *working = cloneInDesign(module, working_id, module->design);
            if (debug)
                log("Keeping a copy of baseline design.\n");

            // 2. Replace synthesized LUTs, clean, freduce, keep track of cells
            log_header(module->design, "Replacing exact synthesized LUTs.\n");
            log_push();
            push_substitution_index(working);
            apply_mapping(working, lut_to_aig_mapping_queue.front(), debug);
            clean_and_freduce(working);
            lut_to_aig_mapping_queue.pop();

            // 3. Evaluate baseline and restore module
            int and_cells_baseline = count_cells(working, "$_AND_");
            if (debug)
                log("\nLogic cells for baseline: %d\n", and_cells_baseline);

            working = cloneInDesign(module, working_id, module->design);
            int best_reward = 0;
            dict<IdString, aig_model_t> best_substitution;
            log_pop();

            // 4. Evaluate alternatives
            log_header(module->design, "Replacing approximate synthesized LUTs.\n");
            log_push();

            // Populate stack
            for (auto cell : working->cells()) {
                if (is_lut(cell)) {
                    auto ax_lut_it = approximated_luts.find(get_lut_param(cell));

                    if (ax_lut_it != approximated_luts.end()) {
                        if (debug)
                            log("Pushing candidate with approximate variant of %s\n", cell->name.c_str());

                        push_substitution_index(working, cell);
                    }
                }
            }

            while (!lut_to_aig_mapping_queue.empty()) {
                apply_mapping(working, lut_to_aig_mapping_queue.front(), debug);
                clean_and_freduce(working);

                int and_cells_candidate = count_cells(working, "$_AND_");
                int reward = and_cells_baseline - and_cells_candidate;
                if (debug) {
                    log("\nLogic cells for candidate: %d\n", and_cells_candidate);
                    log("Current step reward: %d\n", reward);
                }

                if (reward > best_reward) {
                    // TODO Remove hardcoded strings
                    Pass::call(module->design, "stat");
                    Pass::call(working->design, "axmiter -threshold 4 als_golden als_working axmiter");
                    Pass::call_on_module(working->design, working->design->modules_["\\axmiter"], "flatten");
                    if (checkSat(module->design->modules_["\\axmiter"])) {
                        best_reward = reward;
                        best_substitution = lut_to_aig_mapping_queue.front();
                        done_something = true;
                        if (debug)
                            log("\nNew best reward\n");
                    }
                    Pass::call(working->design, "delete axmiter");
                }
                if (debug)
                    log("\n");

                // Restore design
                working = cloneInDesign(module, working_id, module->design);
                lut_to_aig_mapping_queue.pop();
            }

            log_pop();

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

            // TODO Remove hardcoded string
            IdString golden_id = RTLIL::escape_id("als_golden");
            if (first_step) {
                golden = cloneInDesign(module, golden_id, nullptr);
                first_step = false;

                if (debug)
                    log("Keeping a copy of golden design.\n");
            }

            // 1. 4-LUT synthesis
            ScriptPass::call(module->design, "synth -lut 4");
            cloneInDesign(golden, golden_id, module->design);

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

            while (worker.first_step || worker.done_something)
                worker.run_step(top_mod);

            log_pop();
        }
    } AlsPass;
} // namespace yosys_als
