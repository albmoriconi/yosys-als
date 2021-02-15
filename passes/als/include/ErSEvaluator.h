/* -*- c++ -*-
 *  yosys-als -- Approximate logic synthesis
 *
 *  Copyright (C) 2020  Alberto Moriconi <albmoriconi@gmail.com>
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
 * @brief Error rate statistical evaluator for Yosys ALS module
 */

#ifndef YOSYS_ALS_ERSEVALUATOR_H
#define YOSYS_ALS_ERSEVALUATOR_H

#include "Optimizer.h"

#include "graph.h"

#include <boost/dynamic_bitset.hpp>

#include <array>

namespace yosys_als {

class ErSEvaluator {
public:
    /// Type for the value of the solution
    typedef std::array<double, 2> value_t;

    /// Parameters for an optimizer based on this evaluator
    struct parameters_t : public optimizer_parameters_t {
        /// Number of test vectors to be evaluated
        int test_vectors_n = 1000;
    };

    /**
     * @brief Constructor
     */
    explicit ErSEvaluator(optimizer_context_t<ErSEvaluator> *ctx);

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
    static bool dominates(const archive_entry_t<ErSEvaluator> &s1,
                          const archive_entry_t<ErSEvaluator> &s2, double arel_bias = 0.0);

    /**
     * @brief Evaluates the delta of dominance between two solutions
     * @param s1 A solution
     * @param s2 Another solution
     * @return The delta of dominance between the two solutions
     */
    static inline double delta_dom(const archive_entry_t<ErSEvaluator> &s1,
                            const archive_entry_t<ErSEvaluator> &s2) {
        double f1 = fabs(s1.second[0] - s2.second[0]);
        double f2 = fabs(s1.second[1] - s2.second[1]);
        f1 = f1 != 0.0 ? f1 : 1.0;
        f2 = f2 != 0.0 ? f2 : 1.0;

        return f1 * f2;
    }

private:
    // The optimizer context
    optimizer_context_t<ErSEvaluator> *ctx;

    // Private solution evaluation data
    double rel_norm;
    size_t gates_baseline;
    std::vector<boost::dynamic_bitset<>> test_vectors;
    std::vector<boost::dynamic_bitset<>> exact_outputs;

    // Parameters
    size_t test_vectors_n = 1000;

    // Execution data
    unsigned processor_count;

    // Private evaluation methods
    std::vector<boost::dynamic_bitset<>> selection_sample(unsigned long n, unsigned long max) const;

    static std::vector<boost::dynamic_bitset<>> simple_sample(unsigned long n, unsigned long log2max);

    double circuit_reliability(const solution_t &s) const;

    double circuit_reliability_smt(const solution_t &s) const;

    boost::dynamic_bitset<> evaluate_graph(const solution_t &s,
                                           const boost::dynamic_bitset<> &input) const;

    size_t gates(const solution_t &s) const;
};

}

#endif //YOSYS_ALS_ERSEVALUATOR_H
