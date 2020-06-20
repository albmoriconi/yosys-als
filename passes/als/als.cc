/* -*- c++ -*-
 *  yosys-als -- Approximate logic synthesis
 *
 *  Copyright (C) 2019  Alberto Moriconi <albmoriconi@gmail.com>
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
#include "ErSEvaluator.h"
#include "Optimizer.h"
#include "yosys_utils.h"
#include "kernel/yosys.h"

#include <boost/filesystem.hpp>
#include <sqlite3.h>

#include <string>
#include <thread>
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

        /// If \c true, rewrite AIG
        bool rewrite_run = false;

        /// The catalogue cache
        sqlite3 *db = nullptr;

        /// Weights for the outputs
        weights_t weights;

        /// Maximum number of iterations for the optimizer
        size_t max_iter;

        /// Number of test vectors to be evaluated
        size_t test_vectors_n;

        /// Index of the synthesized LUTs
        dict<Const, std::vector<aig_model_t>> synthesized_luts;

        template <typename E>
        static std::string print_archive(const Optimizer<E> &opt, const archive_t<E> &arch) {
            std::string log_string;
            log_string.append(" Entry     Chosen LUTs         Arel        Gates\n");
            log_string.append(" ----- --------------- ------------ ------------\n");

            char log_line_buffer[60];
            for (size_t i = 0; i < arch.size(); i++) {
                auto choice_s = opt.to_string(arch[i].first);
                if (choice_s.size() > 15) {
                    choice_s = choice_s.substr(0, 15);
                }
                snprintf(log_line_buffer, sizeof(log_line_buffer), " %5zu %15s %12g %12g\n",
                        i, choice_s.c_str(), arch[i].second[0], arch[i].second[1]);
                log_string.append(log_line_buffer);
            }

            return log_string;
        }

        void replace_lut(Module *const module, Cell *const lut, const aig_model_t &aig) {
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
            for (size_t i = 0; i < aig.num_gates; i++) {
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
                    int g_idx = aig.num_inputs + i;
                    int p = aig.p[g_idx][c];
                    int s = aig.s[g_idx][c];
                    module->connect(and_ab[c][i], vars[p][s]);
                }
            }
            module->connect(lut_out, vars[aig.out_p][aig.out]);

            // Delete LUT
            module->remove(lut);
        }

        void exact_synthesis_helper(Module *module) {
            auto processor_count = std::thread::hardware_concurrency();
            processor_count = std::min(processor_count, 1u);
            std::vector<dict<Const, std::vector<aig_model_t>>> result_slices(processor_count);

            // In this simple implementation we pay SMT with bookkeeping
            std::set<Yosys::Const> unique_luts_set;
            std::vector<Yosys::Const> unique_luts;
            for (auto cell : module->cells()) {
                if (is_lut(cell)) {
                    unique_luts_set.insert(get_lut_param(cell));
                }
            }
            std::copy(unique_luts_set.begin(), unique_luts_set.end(), std::back_inserter(unique_luts));

            // Ideally, we shouldn't make slices but start threads on demand.
            // Otherwise, we are slowed down by the most computationally intensive slice
            // Inexact semantics probably are not a problem, as we wouldn't start synthesis
            // of the next entry before knowing current size unless we have a spare thread
            // and we decide to do it speculatively.
            // However, speculation is a good call: if current LUT is small enough, it would
            // terminate quick enough to start on same thread; if it doesn't, using another
            // thread seems a good call.
            size_t slice = unique_luts.size() / processor_count;
            std::vector<std::thread> threads;

            for (size_t j = 0; j < processor_count; j++) {
                size_t start = j * slice;
                size_t end = std::min(start + slice, unique_luts.size());

                threads.emplace_back([this, start, end, &unique_luts, &result_slices, j] () {
                    for (size_t i = start; i < end; i++) {
                        const auto &fun_spec = unique_luts[i];

                        result_slices[j][fun_spec] =
                                std::vector<aig_model_t>{synthesize_lut(fun_spec, 0, debug, db)};

                        size_t dist = 1;
                        while (result_slices[j][fun_spec].back().num_gates > 0) {
                            auto approximate_candidate = synthesize_lut(fun_spec, dist++, debug, db);
                            result_slices[j][fun_spec].push_back(std::move(approximate_candidate));
                        }
                    }
                });
            }

            for (auto &t : threads)
                t.join();

            // Sorry, other bookkeeping
            for (auto &result_slice : result_slices)
                for (auto &result : result_slice)
                    synthesized_luts[result.first] = result.second;
        }

        /**
         * Runs an ALS step on selected module
         * @param module A module
         */
        void run(Module *const module) {
            // -1. Ensure our cache db is ready
            sqlite3_open("catalogue.db", &db);

            // 0. Is this a rewrite run?
            if (rewrite_run) {
                log_header(module->design, "Rewriting the AIG.\n");
                Pass::call(module->design, "clean");

                std::vector<Cell*> to_sub;
                for (auto cell : module->cells()) {
                    if (is_lut(cell)) {
                        to_sub.push_back(cell);
                    }
                }

                for (auto cell : to_sub) {
                    replace_lut(module, cell, synthesize_lut(get_lut_param(cell), 0, debug, db));
                }

                Pass::call(module->design, "clean");
                return;
            }

            // 1. 4-LUT synthesis
            ScriptPass::call(module->design, "synth -lut 4");

            // 2. SMT exact synthesis
            log_header(module->design, "Running SMT exact synthesis for LUTs.\n");
            exact_synthesis_helper(module);

            // 3. Optimize circuit and show results
            log_header(module->design, "Running approximation heuristic.\n");
            auto optimizer = Optimizer<ErSEvaluator>(module, weights, synthesized_luts);
            ErSEvaluator::parameters_t parameters;
            parameters.max_iter = max_iter;
            parameters.test_vectors_n = test_vectors_n;
            optimizer.setup(parameters);
            auto archive = optimizer();

            // 4. Save results
            log_header(module->design, "Saving archive of results.\n");
            log_push();
            std::string dir_name("als_");
            dir_name += (module->name.c_str() + 1);
            boost::filesystem::path dir_path(dir_name.c_str());
            boost::filesystem::create_directory(dir_path); // TODO Please check for errors

            auto log_string = print_archive(optimizer, archive);
            std::ofstream log_file;
            log_file.open (dir_name + "/log.txt");
            log_file << log_string;
            log_file.close();

            std::string command = "write_ilang";
            Pass::call(module->design, command + " " + dir_name + "/exact.ilang");

            dict<IdString, Const> to_restore;
            for (auto cell : module->cells()) {
                if (is_lut(cell))
                    to_restore[cell->name] = get_lut_param(cell);
            }

            for (size_t i = 0; i < archive.size(); i++) {
                log_header(module->design, "Rewriting variant %zu.\n", i);
                std::string file_name("variant_");
                file_name += std::to_string(i + 1);
                for (auto &v : archive[i].first) {
                    if (is_lut(v.first.cell)) {
                        auto aig = synthesized_luts[get_lut_param(v.first.cell)][v.second];
                        std::string fun_spec_s;
                        boost::to_string(aig.fun_spec, fun_spec_s);
                        log("Rewriting %s with %s\n",
                                get_lut_param(v.first.cell).as_string().c_str(), fun_spec_s.c_str());
                        v.first.cell->setParam("\\LUT", Const::from_string(fun_spec_s));
                    }
                }
                Pass::call(module->design, command + " " + dir_name + "/" + file_name + ".ilang");

                for (auto cell : module->cells()) {
                    if (is_lut(cell))
                        cell->setParam("\\LUT", to_restore[cell->name]);
                }
            }
            log_header(module->design, "Rolling-back all rewrites.\n");
            log_pop();

            // 5. Output results
            log_header(module->design, "Showing archive of results.\n");
            log(log_string.c_str());

            // +1. Close our db cache
            assert(sqlite3_close(db) == SQLITE_OK);
            db = nullptr;
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
            log("    -i <value>\n");
            log("        set the number of iterations for the optimizer.\n");
            log("\n");
            log("\n");
            log("    -v <value>\n");
            log("        set the number of test vectors for the evaluator.\n");
            log("\n");
            log("\n");
            log("    -r\n");
            log("        run AIG rewriting of top module\n");
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
            std::string max_iter = "2500";
            std::string test_vectors_n = "1000";

            // TODO Add arguments for specifying input probability
            size_t argidx;
            for (argidx = 1; argidx < args.size(); argidx++) {
                if (args[argidx] == "-w" && argidx + 2 < args.size()) {
                    std::string lhs = args[++argidx].c_str();
                    std::string rhs = args[++argidx].c_str();
                    weights.emplace_back(lhs, rhs);
                    continue;
                }
                if (args[argidx] == "-i" && argidx + 1 < args.size()) {
                    std::string arg = args[++argidx].c_str();
                    max_iter = std::stoul(arg);
                    continue;
                }
                if (args[argidx] == "-v" && argidx + 1 < args.size()) {
                    std::string arg = args[++argidx].c_str();
                    test_vectors_n = std::stoul(arg);
                    continue;
                }
                if (args[argidx] == "-d") {
                    worker.debug = true;
                    continue;
                }
                if (args[argidx] == "-r") {
                    worker.rewrite_run = true;
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

            worker.max_iter = std::stoul(max_iter);
            worker.test_vectors_n = std::stoul(test_vectors_n);

            worker.run(top_mod);

            log_pop();
        }
    } AlsPass;
} // namespace yosys_als
