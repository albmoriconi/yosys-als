/* -*- c++ -*-
 *  yosys-als -- Approximate logic synthesis
 *
 *  Copyright (C) 2021  Alberto Moriconi <albmoriconi@gmail.com>
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
 * @brief Maximum absolute error evaluator for Yosys ALS module
 */

#ifndef YOSYS_ALS_EPSMAXEVALUATOR_H
#define YOSYS_ALS_EPSMAXEVALUATOR_H

#include "Optimizer.h"

namespace yosys_als {

class EpsMaxEvaluator {
public:
    /// Type for the value of the solution
    typedef std::array<double, 2> value_t;

    /// Parameters for an optimizer based on this evaluator
    struct parameters_t : public optimizer_parameters_t {};

    /**
     * @brief Constructor
     */
    explicit EpsMaxEvaluator(optimizer_context_t<EpsMaxEvaluator> *ctx);

    /**
     * @brief Setup the evaluator
     * @note This way we complete construction before initialization
     */
    void setup(const parameters_t &parameters);

    /**
     * @brief Evaluates a solution
     * @param s The solution
     */
    value_t value(const solution_t &s) const;

    /**
     * @brief Evaluates a solution that is known to be an empty solution
     * @param s The solution
     */
    static value_t empty_solution_value(const solution_t &s);

    /**
     * @brief Checks if a solution dominates another
     * @param s1 A solution
     * @param s2 Another solution
     * @param arel_bias A "desired value" of areliability
     * @return True if s1 dominates s2, otherwise false
     */
    static bool dominates(const archive_entry_t<EpsMaxEvaluator> &s1,
                          const archive_entry_t<EpsMaxEvaluator> &s2, double arel_bias = 0.0);

    /**
     * @brief Evaluates the delta of dominance between two solutions
     * @param s1 A solution
     * @param s2 Another solution
     * @return The delta of dominance between the two solutions
     */
    static inline double delta_dom(const archive_entry_t<EpsMaxEvaluator> &s1,
                            const archive_entry_t<EpsMaxEvaluator> &s2) {
        double f1 = fabs(s1.second[0] - s2.second[0]);
        double f2 = fabs(s1.second[1] - s2.second[1]);
        f1 = f1 != 0.0 ? f1 : 1.0;
        f2 = f2 != 0.0 ? f2 : 1.0;

        return f1 * f2;
    }

private:
    // The optimizer context
    optimizer_context_t<EpsMaxEvaluator> *ctx;

    // Private solution evaluation data
    size_t gates_baseline;

    // Execution data
    unsigned processor_count;

    // Private evaluation methods
    double circuit_epsmax(const solution_t &s) const;

    boost::dynamic_bitset<> evaluate_graph(const solution_t &s,
                                           const boost::dynamic_bitset<> &input) const;

    size_t gates(const solution_t &s) const;

    std::vector<boost::dynamic_bitset<>> exact_outputs;
};

}

#endif //YOSYS_ALS_EPSMAXEVALUATOR_H
