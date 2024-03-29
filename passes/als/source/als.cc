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

#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "kernel/yosys.h"

#if defined __GNUC__
#pragma GCC diagnostic pop
#endif

#include "AlsWorker.h"

USING_YOSYS_NAMESPACE

/**
 * @brief Yosys ALS module namespace
 */
namespace yosys_als {

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
        log("    -m <metric>\n");
        log("        select the metric (default: ers).\n");
        log("\n");
        log("\n");
        log("    -w <signal> <value>\n");
        log("        set the weight for the output signal to the specified power of two.\n");
        log("\n");
        log("\n");
        log("    -i <value>\n");
        log("        set the number of iterations for the optimizer.\n");
        log("\n");
        log("\n");
        log("    -t <value>\n");
        log("        set the maximum tries for SMT synthesis of approximate LUTs.\n");
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
        std::string max_tries = "20";

        // TODO Add arguments for specifying input probability
        size_t argidx;
        for (argidx = 1; argidx < args.size(); argidx++) {
            if (args[argidx] == "-m" && argidx + 1 < args.size()) {
                worker.metric = args[++argidx];
            } else if (args[argidx] == "-w" && argidx + 2 < args.size()) {
                std::string lhs = args[++argidx];
                std::string rhs = args[++argidx];
                weights.emplace_back(lhs, rhs);
            } else if (args[argidx] == "-i" && argidx + 1 < args.size()) {
                std::string arg = args[++argidx];
                max_iter = arg;
            } else if (args[argidx] == "-t" && argidx + 1 < args.size()) {
                std::string arg = args[++argidx];
                max_tries = arg;
            } else if (args[argidx] == "-v" && argidx + 1 < args.size()) {
                std::string arg = args[++argidx];
                test_vectors_n = arg;
            } else if (args[argidx] == "-d") {
                worker.debug = true;
            } else if (args[argidx] == "-r") {
                worker.rewrite_run = true;
            }
        }
        extra_args(args, argidx, design);

        if (worker.metric.empty()) {
            worker.metric = "ers";
        }

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
        worker.max_tries = std::stoul(max_tries);

        worker.run(top_mod);

        log_pop();
    }
} AlsPass;

} // namespace yosys_als
