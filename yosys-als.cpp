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

#include "lut_synthesis.h"
#include "kernel/yosys.h"
#include "kernel/sigtools.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

// TODO FULL CODE REFACTOR
struct Als : public Pass {
    Als() : Pass("als", "approximate logic synthesis") {}

    void help() YS_OVERRIDE {
        //   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
        log("\n");
        log("    als [options] [top-level]\n");
        log("\n");
        log("This command executes an approximate logic synthesis.\n");
        log("\n");
    }

    void execute(std::vector<std::string> args, RTLIL::Design *design) YS_OVERRIDE {
        log_header(design, "Executing ALS pass (approximate logic synthesis).\n");
        log_push();

        size_t argidx;
        for (argidx = 1; argidx < args.size(); argidx++) { }
        extra_args(args, argidx, design);

        // 1. Select module, save the old one
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

        auto *design_copy = new RTLIL::Design;
        for (auto &module : design->modules_)
            design_copy->add(module.second->clone());

        // 2. 4-LUT synthesis
        ScriptPass::call(design, "synth -lut 4");
        log("\n");

        // 3. Create signal -> driver dictionary (in case we need it next...)
        update_driver_index(top_mod);

        // 4. SMT LUT synthesis
        for (auto cell : top_mod->cells()) {
            if (cell->hasParam("\\LUT")) {
                smt::aig_model aig = smt::lut_synthesis(cell->getParam("\\LUT").as_string(), 0);
                // TODO Handle crash on second ALS run
                // TODO Handle constant case

                if (debug) {
                    log("=== %s ===\n\n", cell->name.c_str());
                    log("   LUT number: %s\n", cell->getParam("\\LUT").as_string().c_str());
                    log("   AIG (%zu IN %zu AND):\n", aig.num_inputs, aig.s.size());
                    for (size_t i = 0; i < aig.s.size(); i++)
                        log("      %zu : AND(%dp%d, %dp%d)\n", i + aig.num_inputs + 1,
                            aig.s[i].first, aig.p[i].first, aig.s[i].second, aig.p[i].second);
                    log("\n");
                }

                synthesized_luts[cell->name] = aig;
            }
        }

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
            top_mod->remove(top_mod->cell(lut.first));
        }

        // 5. Print stats
        Pass::call(design, "opt_clean");
        Pass::call(design, "abc -script +strash;ifraig;dc2");
        Pass::call(design, "stat");

        log_pop();
    }

    void update_driver_index(RTLIL::Module *mod) {
        SigMap sigmap(mod);
        sigbit_to_driver_index.clear();

        for (auto cell : mod->cells())
            for (auto &conn : cell->connections())
                if (cell->output(conn.first))
                    for (auto &bit : sigmap(conn.second)) {
                        sigbit_to_driver_index[bit] = cell;
                        if (debug)
                            log("Indexed %s -> %s\n", bit.wire->name.c_str(), cell->name.c_str());
                    }

        if (debug) log("\n");
    }

    dict<SigBit, Cell*> sigbit_to_driver_index;
    bool debug = true;

    dict<IdString, smt::aig_model> synthesized_luts;

    struct and_inv_ports {
        Wire *in1;
        Wire *in2;
        Wire *outp1;
        Wire *outp0;
    };
} AlsPass;

PRIVATE_NAMESPACE_END
