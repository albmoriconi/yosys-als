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
 * @brief pproximate logic synthesis worker for Yosys ALS module
 */

#ifndef YOSYS_ALS_ALSWORKER_H
#define YOSYS_ALS_ALSWORKER_H

#include "Optimizer.h"
#include "smtsynth.h"

#include "kernel/yosys.h"

#include <sqlite3.h>

#include <string>
#include <vector>

namespace yosys_als {

/**
 * @brief Worker for the ALS pass
 */
class AlsWorker {
public:
    /// If \c true, log debug information
    bool debug = false;

    /// If \c true, rewrite AIG
    bool rewrite_run = false;

    /// The metric to be used for evaluation @todo make a pointer to class
    std::string metric;

    /// Weights for the outputs
    weights_t weights;

    /// Maximum number of iterations for the optimizer
    size_t max_iter{};

    /// Number of test vectors to be evaluated
    size_t test_vectors_n{};

    /// Index of the synthesized LUTs
    Yosys::dict<Yosys::Const, std::vector<aig_model_t>> synthesized_luts;

    /**
     * Runs an ALS step on selected module
     * @param module A module
     */
    void run(Yosys::Module *const module);

private:
    sqlite3 *db = nullptr;

    template<typename E>
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

    void replace_lut(Yosys::Module *const module, Yosys::Cell *const lut, const aig_model_t &aig);

    void exact_synthesis_helper(Yosys::Module *module);

    template<typename E>
    std::string optimizeAndRewrite(Yosys::Module *const module, typename E::parameters_t parameters);
};

}

#endif //YOSYS_ALS_ALSWORKER_H
