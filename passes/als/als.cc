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
#include "Optimizer.h"
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

        /// Weights for the outputs
        Optimizer::weights_t weights;

        /// Index of the synthesized LUTs
        dict<Const, std::vector<mig_model_t>> synthesized_luts;

        void replace_lut(Module *const module, Cell *const lut, const mig_model_t &mig) {
            // Vector of variables in the model
            std::array<SigSpec, 2> vars;
            vars[1].append(State::S0);

            // Get LUT ins and outs
            SigSpec lut_out;
            for (auto &conn : lut->connections()) {
                if (lut->input(conn.first))
                    vars[1].append(conn.second);
                else if (lut->output(conn.first))
                    lut_out = conn.second;
            }

            // Create AND gates
            std::array<std::vector<Wire *>, 2> and_ab;
            for (size_t i = 0; i < mig.num_gates; i++) {
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
                    int g_idx = mig.num_inputs + i;
                    int p = mig.p[g_idx][c];
                    int s = mig.s[g_idx][c];
                    module->connect(and_ab[c][i], vars[p][s]);
                }
            }
            module->connect(lut_out, vars[mig.out_p][mig.out]);

            // Delete LUT
            module->remove(lut);
        }

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

                        size_t dist = 1;
                        while (synthesized_luts[fun_spec].back().num_gates > 0) {
                            auto approximate_candidate = synthesize_lut(fun_spec, dist++, debug);
                            synthesized_luts[fun_spec].push_back(std::move(approximate_candidate));
                        }
                    }
                }
            }

            // 3. Optimize circuit
            log_header(module->design, "Running approximation heuristic.\n");
            auto optimizer = Optimizer(module, weights, synthesized_luts);
            for (auto &sub : optimizer()) {
                replace_lut(module, sub.first.cell, synthesized_luts[get_lut_param(sub.first.cell)][sub.second]);
            }
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
            log("    -w <signal> <value>\n");
            log("        set the weight for the output signal to the specified power of two.\n");
            log("\n");
            log("\n");
            log("    -d\n");
            log("        enable debug output\n");
            log("\n");
        }

        void execute(std::vector<std::string> args, Design *design) YS_OVERRIDE {
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

            for (auto &w : weights) {
                RTLIL::SigSpec lhs;
                if (!RTLIL::SigSpec::parse_sel(lhs, design, top_mod, w.first))
                    log_cmd_error("Failed to parse lhs weight expression `%s'.\n", w.first.c_str());
                if (!lhs.is_wire() || !lhs.as_wire()->port_output)
                    log_cmd_error("Lhs weight expression `%s' not an output.\n", w.first.c_str());

                worker.weights[lhs] = std::stod(w.second);
            }

            worker.run(top_mod);

            log_pop();
        }
    } AlsPass;
} // namespace yosys_als
