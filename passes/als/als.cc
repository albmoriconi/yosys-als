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
#include "utils.h"
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

        /// Index of the synthesized LUTs (kept between steps)
        dict<Const, mig_model_t> synthesized_luts;

        /// Index of the approximately synthesized LUTs (kept between steps)
        dict<Const, std::vector<mig_model_t>> approximated_luts;

        // TODO CONSIDER REMOVING THIS
        void apply_mapping(Module *const module) {
            dict<IdString, mig_model_t> mapping;

            for (auto cell : module->cells()) {
                if (is_lut(cell)) {
                    auto lut_it = synthesized_luts.find(get_lut_param(cell));

                    if (lut_it != synthesized_luts.end())
                        mapping[cell->name] = lut_it->second;
                }
            }

            for (auto &sub : mapping) {
                replace_lut(module, sub);

                if (debug)
                    log("Replaced %s in %s.\n", sub.first.c_str(), module->name.c_str());
            }
        }

        static void replace_lut(Module *const module, const pair<IdString, mig_model_t> &lut) {
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

            // Create Maj gates
            std::array<std::vector<Wire *>, 3> maj_abc;
            for (size_t i = 0; i < lut.second.num_gates; i++) {
                Wire *maj_a = module->addWire(NEW_ID);
                Wire *maj_b = module->addWire(NEW_ID);
                Wire *maj_c = module->addWire(NEW_ID);
                Wire *maj_y = module->addWire(NEW_ID);
                Cell *c = module->addCell(NEW_ID, "\\MAJ");
                c->setPort("\\A", maj_a);
                c->setPort("\\B", maj_b);
                c->setPort("\\C", maj_c);
                c->setPort("\\Y", maj_y);
                maj_abc[0].push_back(maj_a);
                maj_abc[1].push_back(maj_b);
                maj_abc[2].push_back(maj_c);
                vars[1].append(maj_y);
            }

            // Negate variables
            for (auto &sig : vars[1]) {
                Wire *not_y = module->addWire(NEW_ID);
                module->addNotGate(NEW_ID, sig, not_y);
                vars[0].append(not_y);
            }

            // Create connections
            assert(GetSize(maj_abc[0]) == GetSize(maj_abc[1]) && GetSize(maj_abc[1]) == GetSize(maj_abc[2]));
            assert(GetSize(vars[0]) == GetSize(vars[1]));
            for (size_t i = 0; i < maj_abc[0].size(); i++) {
                for (size_t c = 0; c < maj_abc.size(); c++) {
                    size_t g_idx = lut.second.num_inputs + i;
                    size_t p = lut.second.p[g_idx][c];
                    size_t s = lut.second.s[g_idx][c];
                    module->connect(maj_abc[c][i], vars[p][s]);
                }
            }
            module->connect(lut_out, vars[lut.second.out_p][lut.second.out]);

            // Delete LUT
            module->remove(module->cell(lut.first));
        }
        // END OF REMOVAL

        /**
         * Runs an ALS step on selected module
         * @param module A module
         */
        void run(Module *module) {
            // 0. Read Maj cell
            std::string maj_verilog = "module MAJ(A, B, C, Y);\n"
                                      "input A, B, C;\n"
                                      "output Y;\n"
                                      "assign Y = (A & B) | (A & C) | (B & C);\n"
                                      "endmodule\n";
            std::istringstream f(maj_verilog);
            auto *map = new RTLIL::Design;
            Frontend::frontend_call(map, &f, "<maj.v>", "verilog -nooverwrite -noblackbox");

            // 1. 4-LUT synthesis
            ScriptPass::call(module->design, "synth -lut 4");

            // 2. SMT exact synthesis
            log_header(module->design, "Running SMT exact synthesis for LUTs.\n");
            for (auto cell : module->cells()) {
                if (is_lut(cell)) {
                    const auto &fun_spec = get_lut_param(cell);

                    if (synthesized_luts.find(fun_spec) == synthesized_luts.end()) {
                        synthesized_luts[fun_spec] = synthesize_lut(fun_spec, 0, debug);
                        //approximated_luts[fun_spec] = vector<mig_model_t>();

                        // TODO Add multiple approximate candidates
                        //if (synthesized_luts[fun_spec].num_gates > 0) {
                        //    auto approximate_candidate = synthesize_lut(fun_spec, 1, debug);
                        //    if (approximate_candidate.num_gates < synthesized_luts[fun_spec].num_gates)
                        //        approximated_luts[fun_spec].push_back(std::move(approximate_candidate));
                        //}
                    }
                }
            }
            apply_mapping(module);
            delete map;

            // 3. Create a graph structure

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
