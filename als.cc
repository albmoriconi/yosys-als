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

#include "smtsynth.h"
#include "kernel/yosys.h"

using namespace smtsynth;

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct AlsWorker {
    bool debug = false;
    dict<std::string, aig_model_t> synthesized_luts;
    dict<std::string, aig_model_t> approximated_luts;

    aig_model_t synthesize_lut(const Cell *const cell, const int ax_degree) {
        if (debug) {
            log("\n=== LUT %s ===\n", cell->getParam("\\LUT").as_string().c_str());
            if (ax_degree > 0)
                log("\n   Approximation degree: %d\n\n", ax_degree);
            else
                log("\n");
        }

        aig_model_t aig = lut_synthesis(cell->getParam("\\LUT").as_string(), ax_degree);

        if (debug) {
            for (int i = 0; i < GetSize(aig.s); i++) {
                log("      %d\t\t\t", i);
                if (i == 0)
                    log("C0\n");
                else if (i < aig.num_inputs)
                    log("PI\n");
                else
                    log("AND(%dp%d, %dp%d)\n", aig.s[i][0], aig.p[i][0], aig.s[i][1], aig.p[i][1]);
            }
            log("      Out\t\t%dp%d\n", aig.out, aig.out_p);
        }

        return aig;
    }

    void replace_synthesized_luts(Module *const top_mod) const {
        dict<IdString, aig_model_t> sub_index;
        for (auto cell : top_mod->cells()) {
            if (cell->hasParam("\\LUT")) {
                auto fun_spec = cell->getParam("\\LUT").as_string();
                auto lut_it = synthesized_luts.find(fun_spec);

                if (lut_it != synthesized_luts.end())
                    sub_index[cell->name] = lut_it->second;
            }
        }

        for (auto &sub : sub_index) {
            replace_lut(top_mod, sub);
            if (debug)
                log("Replaced %s in %s.\n", sub.first.c_str(), top_mod->name.c_str());
        }
    }

    int count_and_cells(Module *top_mod) const {
        int and_cells = 0;

        for (auto cell : top_mod->cells()) {
            if (cell->type == "$_AND_")
                and_cells++;
        }

        return and_cells;
    }

    void replace_approximated_greedy(Module *top_mod) const {
        log_header(top_mod->design, "Finding best approximate LUT substitution.\n");
        log_push();

        // 0. Some book keping
        Design *design = top_mod->design;
        IdString top_mod_id = top_mod->name;
        IdString working_id = RTLIL::escape_id("als_working");

        // 1. Save module
        Module *working = top_mod->clone();
        working->name = working_id;
        working->design = design;
        working->attributes.erase("\\top");
        design->modules_[working_id] = working;
        if (debug)
            log("Keeping a copy of baseline design.\n");

        // 2. Replace synthesized LUTs, clean, freduce, keep track of cells
        log_header(design, "Replacing exact synthesized LUTs.\n");
        replace_synthesized_luts(working);
        Pass::call_on_module(design, working, "opt_clean");
        Pass::call_on_module(design, working, "freduce");
        Pass::call_on_module(design, working, "opt_clean");

        // 3. Evaluate baseline
        int and_cells_baseline = count_and_cells(working);
        if (debug)
            log("\nLogic cells for baseline: %d\n", and_cells_baseline);
        int best_reward = 0;

        working = top_mod->clone();
        working->name = working_id;
        working->design = design;
        working->attributes.erase("\\top");
        design->modules_[working_id] = working;
        log_header(design, "Replacing approximate synthesized LUTs.\n");

        dict<IdString, aig_model_t> sub_index;
        dict<IdString, aig_model_t> ax_sub_index;
        for (auto cell : working->cells()) {
            if (cell->hasParam("\\LUT")) {
                auto fun_spec = cell->getParam("\\LUT").as_string();
                auto lut_it = synthesized_luts.find(fun_spec);
                auto ax_lut_it = approximated_luts.find(fun_spec);

                if (lut_it != synthesized_luts.end())
                    sub_index[cell->name] = lut_it->second;
                if (ax_lut_it != approximated_luts.end())
                    ax_sub_index[cell->name] = ax_lut_it->second;
            }
        }

        std::pair<IdString, aig_model_t> best_sub;
        for (auto &axlut : ax_sub_index) {
            replace_lut(working, axlut);
            if (debug)
                log("Replaced %s (approximate) in %s.\n", axlut.first.c_str(), working->name.c_str());

            for (auto &exlut : sub_index) {
                if (exlut.first != axlut.first) {
                    replace_lut(working, exlut);
                    if (debug)
                        log("Replaced %s in %s.\n", exlut.first.c_str(), working->name.c_str());
                }
            }

            Pass::call_on_module(top_mod->design, working, "opt_clean");
            Pass::call_on_module(top_mod->design, working, "freduce");
            Pass::call_on_module(top_mod->design, working, "opt_clean");

            int and_cells_candidate = count_and_cells(working);

            if (debug)
                log("\nLogic cells for candidate: %d\n", and_cells_candidate);

            int reward = and_cells_baseline - and_cells_candidate;
            if (debug)
                log("Current step reward: %d\n", reward);

            if (reward > best_reward) {
                // Check with axmiter
                best_reward = reward;
                best_sub = axlut;
                if (debug)
                    log("New best reward\n");
            }
            if (debug)
                log("\n");

            // Restore design
            working = top_mod->clone();
            working->name = working_id;
            working->design = design;
            working->attributes.erase("\\top");
            design->modules_[working_id] = working;
        }

        // Restore best
        log("Best reward: %d\n", best_reward);
        replace_lut(top_mod, best_sub);
        if (debug)
            log("Replaced %s in %s.\n", best_sub.first.c_str(), top_mod->name.c_str());
        log_pop();
        Pass::call_on_module(design, top_mod, "opt_clean");
        Pass::call_on_module(design, top_mod, "freduce");
        Pass::call_on_module(design, top_mod, "opt_clean");
    }

    void replace_lut(Module *const top_mod, const pair<IdString, aig_model_t> &lut) const {
        // Vector of variables in the model
        std::array<SigSpec, 2> vars;
        vars[1].append(State::S0);

        // Get LUT ins and outs
        SigSpec lut_out;
        for (auto &conn : top_mod->cell(lut.first)->connections()) {
            if (top_mod->cell(lut.first)->input(conn.first))
                vars[1].append(conn.second);
            else if (top_mod->cell(lut.first)->output(conn.first))
                lut_out = conn.second;
        }

        // Create AND gates
        std::array<std::vector<Wire *>, 2> and_ab;
        for (int i = 0; i < lut.second.num_gates; i++) {
            Wire *and_a = top_mod->addWire(NEW_ID);
            Wire *and_b = top_mod->addWire(NEW_ID);
            Wire *and_y = top_mod->addWire(NEW_ID);
            top_mod->addAndGate(NEW_ID, and_a, and_b, and_y);
            and_ab[0].push_back(and_a);
            and_ab[1].push_back(and_b);
            vars[1].append(and_y);
        }

        // Negate variables
        for (auto &sig : vars[1]) {
            Wire *not_y = top_mod->addWire(NEW_ID);
            top_mod->addNotGate(NEW_ID, sig, not_y);
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
                top_mod->connect(and_ab[c][i], vars[p][s]);
            }
        }
        top_mod->connect(lut_out, vars[lut.second.out_p][lut.second.out]);

        // Delete LUT
        top_mod->remove(top_mod->cell(lut.first));
    }

    void run(Module *top_mod) {
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

struct AlsPass : public Pass {
    AlsPass() : Pass("als", "approximate logic synthesis") {}

    void help() YS_OVERRIDE {
        //   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
        log("\n");
        log("    als [options]\n");
        log("\n");
        log("This command executes an approximate logic synthesis.\n");
        log("\n");
        log("    -d\n");
        log("        enable debug output\n");
        log("\n");
    }

    void execute(std::vector<std::string> args, Design *design) YS_OVERRIDE {
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
