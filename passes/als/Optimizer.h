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
#include "kernel/sigtools.h"

#include <Eigen/Dense>

#include <random>

namespace yosys_als {

    /**
     * @brief Implements the optimization heuristic for ALS
     */
    class Optimizer {
    public:
        /// Type for a solution
        typedef Yosys::dict<vertex_t, size_t> solution_t;
        typedef std::array<double, 2> value_t;
        typedef std::pair<solution_t, value_t> archive_entry_t;
        typedef std::vector<archive_entry_t> archive_t;

        typedef Yosys::dict<Yosys::SigBit, double> weights_t;

        /**
         * @brief Constructs an optimizer
         * @param module A module
         * @param luts The lut catalogue for the model
         */
        Optimizer(Yosys::Module *module, weights_t &weights, lut_catalogue_t &luts);

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
        // Private solution evaluation types
        typedef Yosys::dict<vertex_t, double> reliability_index_t;
        typedef Eigen::Matrix2d z_matrix_t;
        typedef Eigen::MatrixXd matrix_double_t;
        typedef Eigen::Matrix<bool, Eigen::Dynamic, Eigen::Dynamic> matrix_bool_t;
        typedef Yosys::dict<vertex_t, z_matrix_t> z_matrix_index_t;

        // Private data
        graph_t g;
        std::vector<vertex_d> vertices;
        Yosys::SigMap sigmap;
        weights_t &weights;
        double rel_norm;
        lut_catalogue_t &luts;

        // Private solution evaluation data
        size_t gates_baseline;

        // Static members
        static std::default_random_engine generator;

        // Parameters
        // TODO Tweak parameters (e.g. temp = 5*luts, iter = 4*temp, ...)
        static constexpr size_t soft_limit = 20;
        static constexpr double t_max = 500;
        static constexpr double t_min = 0.01;
        static constexpr double cooling = 0.9;
        static constexpr size_t max_iter = 200;
        static constexpr double arel_bias = 0.2;

        // Private methods
        archive_entry_t empty_solution() const;
        archive_entry_t hill_climb(const archive_entry_t &s) const;
        archive_entry_t neighbor_of(const archive_entry_t &s) const;
        bool dominates(const archive_entry_t &s1, const archive_entry_t &s2) const;
        value_t value(const solution_t &s) const;
        //static double accept_probability(const value_t &c1, const value_t &c2, double temp);

        // Private solution evaluation methods
        double circuit_reliability(const reliability_index_t &all_the_rels) const;
        reliability_index_t output_reliability(const solution_t &s) const;
        z_matrix_t z_in_degree_0(const vertex_d &v) const;
        z_matrix_t z_in_degree_pos(const solution_t &s, const vertex_d &v, const z_matrix_index_t &z_matrix_for) const;
        double reliability_from_z(const z_matrix_t &z) const;
        size_t gates(const solution_t &s) const;
    };
}

#endif //YOSYS_ALS_OPTIMIZER_H
