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
    dict<IdString, aig_model_t> synthesized_luts;
    dict<IdString, aig_model_t> approximated_luts;

    aig_model_t synthesize_lut(const Cell *const cell, const int ax_degree) {
        if (debug) {
            log("\n=== %s ===\n\n", cell->name.c_str());
            log("   LUT function: %s\n", cell->getParam("\\LUT").as_string().c_str());
            if (ax_degree > 0)
                log("   Approximation degree: %d\n\n", ax_degree);
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
        for (auto &lut : synthesized_luts) {
            replace_lut(top_mod, lut);
            if (debug)
                log("Replaced %s.\n", lut.first.c_str());
        }
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
                synthesized_luts[cell->name] = synthesize_lut(cell, 0);
                if (synthesized_luts[cell->name].num_gates > 0) {
                    approximated_luts[cell->name] = synthesize_lut(cell, 1);
                    if (approximated_luts[cell->name].num_gates < synthesized_luts[cell->name].num_gates)
                        log("\nKeeping approximate candidate for %s.\n", cell->name.c_str());
                    else
                        approximated_luts.erase(cell->name);
                }
            }
        }
        if (debug)
            log("\n");

        // 3. Replace synthesized LUTs
        log_header(top_mod->design, "Replacing synthesized LUTs.\n");
        replace_synthesized_luts(top_mod);

        // 4. Clean and reduce
        Pass::call(top_mod->design, "opt_clean");
        //Pass::call(top_mod->design, "abc -script +strash;ifraig;dc2");
        Pass::call(top_mod->design, "freduce");
        Pass::call(top_mod->design, "opt_clean");

        // 5. Print stats
        Pass::call(top_mod->design, "stat");
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
