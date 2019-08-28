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

/**
 * @file
 * @brief Approximate logic synthesis pass for Yosys ALS module
 */

#include "smtsynth.h"
#include "kernel/yosys.h"

#include <boost/dynamic_bitset.hpp>

#include <queue>
#include <string>
#include <vector>

using yosys_als::aig_model_t;
using yosys_als::synthesize_lut;

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

        /// Index of the synthesized LUTs (kept between steps)
        dict<Const, aig_model_t> synthesized_luts;

        /// Index of the approximately synthesized LUTs (kept between steps)
        dict<Const, aig_model_t> approximated_luts;

        /// A queue of the possible LUT-to-AIG mappings
        std::queue<dict<IdString, aig_model_t>> lut_to_aig_mapping_queue;

        /**
         * @brief Wrapper for \c synthesize_lut
         * @param lut The LUT specification
         * @param ax_degree The approximation degree
         * @return The synthesized AIG model
         */
        aig_model_t synthesize_lut(const Const &lut, const unsigned ax_degree) const {
            if (debug)
                log("LUT %s Ax: %d... ", lut.as_string().c_str(), ax_degree);

            auto aig = yosys_als::synthesize_lut(boost::dynamic_bitset<>(lut.as_string()), ax_degree);

            if (debug)
                log("satisfied with %d gates.\n", aig.num_gates);

            return aig;
        }

        /**
         * @brief Applies LUT-to-AIG mapping to module
         * @param module The module (modified in place)
         * @param mapping The LUT-to-AIG mapping
         */
        void apply_mapping(Module *const module, const dict<IdString, aig_model_t> &mapping) const {
            for (auto &sub : mapping) {
                replace_lut(module, sub);

                if (debug)
                    log("Replaced %s in %s.\n", sub.first.c_str(), module->name.c_str());
            }
        }

        // TODO We can do better than take a pointer for ax_cell
        // Alternatives:
        //  - A variant type
        //  - A vector
        void push_substitution_index(Module *const module, Cell *const ax_cell = nullptr) {
            dict<IdString, aig_model_t> substitution_index;

            if (ax_cell != nullptr)
                substitution_index[ax_cell->name] = approximated_luts[ax_cell->getParam("\\LUT").as_string()];

            for (auto cell : module->cells()) {
                if (cell != ax_cell && cell->hasParam("\\LUT")) {
                    auto fun_spec = cell->getParam("\\LUT").as_string();
                    auto lut_it = synthesized_luts.find(fun_spec);

                    if (lut_it != synthesized_luts.end())
                        substitution_index[cell->name] = lut_it->second;
                }
            }

            lut_to_aig_mapping_queue.push(substitution_index);
        }

        int count_and_cells(Module *module) const {
            int and_cells = 0;

            for (auto cell : module->cells()) {
                if (cell->type == "$_AND_")
                    and_cells++;
            }

            return and_cells;
        }

        Module *cloneInSameDesign(const Module *source, const IdString &copy_id) const {
            Module *copy = source->clone();
            copy->name = copy_id;
            copy->design = source->design;
            copy->attributes.erase("\\top");
            source->design->modules_[copy_id] = copy;
            return copy;
        }

        static void post_substitution(Module *module) {
            Pass::call_on_module(module->design, module, "opt_clean");
            Pass::call_on_module(module->design, module, "freduce");
            Pass::call_on_module(module->design, module, "opt_clean");
        }

        // TODO This can be better
        // We should create our SAT instance and check it
        bool checkSat(Module *axmiter) const {
            std::ifstream oldFile("axmiter.json");
            if (oldFile.good())
                std::remove("axmiter.json");

            Pass::call(axmiter->design, "sat -prove trigger 0 -dump_json axmiter.json axmiter");

            std::ifstream newFile("axmiter.json");
            if (newFile.good()) {
                std::remove("axmiter.json");
                return false;
            }

            return true;
        }

        void replace_approximated_greedy(Module *top_mod) {
            log_header(top_mod->design, "Finding best approximate LUT substitution.\n");
            log_push();

            // 0. Some book keeping
            IdString top_mod_id = top_mod->name;
            IdString working_id = RTLIL::escape_id("als_working");
            IdString golden_id = RTLIL::escape_id("als_golden");

            if (first_step) {
                cloneInSameDesign(top_mod, golden_id);
                if (debug)
                    log("Keeping a copy of golden design.\n");
            }

            // 1. Make a working copy
            Module *working = cloneInSameDesign(top_mod, working_id);
            if (debug)
                log("Keeping a copy of baseline design.\n");

            // 2. Replace synthesized LUTs, clean, freduce, keep track of cells
            log_header(top_mod->design, "Replacing exact synthesized LUTs.\n");
            log_push();
            push_substitution_index(working);
            apply_mapping(working);
            post_substitution(working);
            lut_to_aig_mapping_queue.pop();

            // 3. Evaluate baseline and restore module
            int and_cells_baseline = count_and_cells(working);
            if (debug)
                log("\nLogic cells for baseline: %d\n", and_cells_baseline);

            working = cloneInSameDesign(top_mod, working_id);
            int best_reward = 0;
            dict<IdString, aig_model_t> best_substitution;
            log_pop();

            // 4. Evaluate alternatives
            log_header(top_mod->design, "Replacing approximate synthesized LUTs.\n");
            log_push();

            // Populate stack
            for (auto cell : working->cells()) {
                if (cell->hasParam("\\LUT")) {
                    auto fun_spec = cell->getParam("\\LUT").as_string();
                    auto ax_lut_it = approximated_luts.find(fun_spec);

                    if (ax_lut_it != approximated_luts.end()) {
                        if (debug)
                            log("Pushing candidate with approximate variant of %s\n", cell->name.c_str());

                        push_substitution_index(working, cell);
                    }
                }
            }

            while (!lut_to_aig_mapping_queue.empty()) {
                apply_mapping(working);
                post_substitution(working);

                int and_cells_candidate = count_and_cells(working);
                int reward = and_cells_baseline - and_cells_candidate;
                if (debug) {
                    log("\nLogic cells for candidate: %d\n", and_cells_candidate);
                    log("Current step reward: %d\n", reward);
                }

                if (reward > best_reward) {
                    // TODO Remove hardcoded strings
                    Pass::call(working->design, "axmiter -threshold 4 als_golden als_working axmiter");
                    Pass::call_on_module(working->design, working->design->modules_["\\axmiter"], "flatten");
                    if (checkSat(top_mod->design->modules_["\\axmiter"])) {
                        best_reward = reward;
                        best_substitution = lut_to_aig_mapping_queue.top();
                        done_something = true;
                        if (debug)
                            log("\nNew best reward\n");
                    }
                    Pass::call(working->design, "delete axmiter");
                }
                if (debug)
                    log("\n");

                // Restore design
                working = cloneInSameDesign(top_mod, working_id);
                lut_to_aig_mapping_queue.pop();
            }

            log_pop();

            // Restore best
            log("Best reward: %d\n", best_reward);
            if (done_something)
                lut_to_aig_mapping_queue.push(best_substitution);
            else
                push_substitution_index(top_mod);

            apply_mapping(top_mod);
            post_substitution(top_mod);
            lut_to_aig_mapping_queue.pop();
            log_pop();
        }

        void replace_lut(Module *const module, const pair<IdString, aig_model_t> &lut) const {
            // Vector of variables in the model
            std::array<SigSpec, 2> vars;
            vars[1].append(State::S0);

            // Get LUT ins and outs
            SigSpec lut_out;
            for (auto &conn : module->cell(lut.first)->connections()) {
                if (module->cell(lut.first)->input(conn.first))
                    vars[1].append(conn.second);
                else if (module->cell(lut.first)->output(conn.first))
                    lut_out = conn.second;
            }

            // Create AND gates
            std::array<std::vector<Wire *>, 2> and_ab;
            for (int i = 0; i < lut.second.num_gates; i++) {
                Wire *and_a = module->addWire(NEW_ID);
                Wire *and_b = module->addWire(NEW_ID);
                Wire *and_y = module->addWire(NEW_ID);
                module->addAndGate(NEW_ID, and_a, and_b, and_y);
                and_ab[0].push_back(and_a);
                and_ab[1].push_back(and_b);
                vars[1].append(and_y);
            }

            // Negate variables
            for (auto &sig : vars[1]) {
                Wire *not_y = module->addWire(NEW_ID);
                module->addNotGate(NEW_ID, sig, not_y);
                vars[0].append(not_y);
            }

            // Create connections
            assert(GetSize(and_ab[0]) == GetSize(and_ab[1]));
            assert(GetSize(vars[0]) == GetSize(vars[1]));
            for (int i = 0; i < GetSize(and_ab[0]); i++) {
                for (int c = 0; c < GetSize(and_ab); c++) {
                    int g_idx = lut.second.num_inputs + i;
                    int p = lut.second.p[g_idx][c];
                    int s = lut.second.s[g_idx][c];
                    module->connect(and_ab[c][i], vars[p][s]);
                }
            }
            module->connect(lut_out, vars[lut.second.out_p][lut.second.out]);

            // Delete LUT
            module->remove(module->cell(lut.first));
        }

        void run(Module *top_mod) {
            done_something = false;

            // 1. 4-LUT synthesis
            ScriptPass::call(top_mod->design, "synth -lut 4");
            log("\n");

            // 2. SMT exact synthesis
            log_header(top_mod->design, "Running SMT exact synthesis for LUTs.\n");
            for (auto cell : top_mod->cells()) {
                if (cell->hasParam("\\LUT")) {
                    std::string fun_spec = cell->getParam("\\LUT").as_string();

                    if (synthesized_luts.find(fun_spec) == synthesized_luts.end()) {
                        synthesized_luts[fun_spec] = synthesize_lut(cell, 0);

                        if (synthesized_luts[fun_spec].num_gates > 0) {
                            approximated_luts[fun_spec] = synthesize_lut(cell, 1);
                            if (approximated_luts[fun_spec].num_gates < synthesized_luts[fun_spec].num_gates)
                                log("\nKeeping approximate candidate\n");
                            else
                                approximated_luts.erase(fun_spec);
                        }
                    }
                }
            }
            if (debug)
                log("\n");

            // 3. Replace greedily
            replace_approximated_greedy(top_mod);
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
                worker.run(top_mod);

            log_pop();
        }
    } AlsPass;
} // namespace yosys_als
