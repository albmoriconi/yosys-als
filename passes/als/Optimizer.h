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
 * @brief Optimization heuristic for Yosys ALS module
 */

#ifndef YOSYS_ALS_OPTIMIZER_H
#define YOSYS_ALS_OPTIMIZER_H

#include "graph.h"
#include "smtsynth.h"
#include "yosys_utils.h"
#include "kernel/yosys.h"

#include <random>

namespace yosys_als {

    /**
     * @brief Implements the optimization heuristic for ALS
     */
    class Optimizer {
    public:
        /// Type for a solution
        typedef Yosys::dict<vertex_t, size_t> solution_t;

        /**
         * @brief Constructs an optimizer
         * @param module A module
         * @param luts The lut catalogue for the model
         */
        Optimizer(Yosys::Module *module, lut_catalogue_t &luts);

        /**
         * @brief Executes the heuristic optimization
         * @return A local optimum for the problem
         */
        solution_t operator()();

        /**
         * Converts a solution to a string
         * @param s A solution
         * @return A string representation of the solution
         */
        std::string to_string(solution_t &s) const;

    private:
        // Private types
        typedef std::pair<double, size_t> value_t;
        typedef double cost_t;

        // Private data
        graph_t g;
        std::vector<vertex_d> vertices;
        lut_catalogue_t &luts;

        // Static members
        static const std::default_random_engine generator;

        // Parameters
        // TODO Tweak parameters (e.g. temp = 5*luts, iter = 4*temp)
        static constexpr double t_0 = 250;
        static constexpr double alpha = 0.8;
        static constexpr size_t max_iter = 1000;

        // Private methods
        solution_t empty_solution() const;
        solution_t neighbor_of(const solution_t &s) const;
        bool dominates(const solution_t &s1, const solution_t &s2) const;
        value_t value(const solution_t &s) const;
        cost_t cost(const solution_t &s) const;
        double accept_probability(const solution_t &s, const solution_t &s_tick, const double temp);

        // Private solution evaluation methods
        // ...
    };
}

#endif //YOSYS_ALS_OPTIMIZER_H
