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

#ifndef YOSYS_ALS_EVALUATOR_H
#define YOSYS_ALS_EVALUATOR_H

#include "kernel/yosys.h"

#include <Eigen/Dense>

class Evaluator {
    typedef Yosys::dict<vertex_t, double> reliability_index_t;
    double circuit_reliability(const reliability_index_t &all_the_rels) const;
    reliability_index_t output_reliability(const solution_t &s) const;
    size_t gates(const solution_t &s) const;

private:
    // Private solution evaluation types
    typedef Eigen::Matrix2d z_matrix_t;
    typedef Eigen::MatrixXd matrix_double_t;
    typedef Eigen::Matrix<bool, Eigen::Dynamic, Eigen::Dynamic> matrix_bool_t;
    typedef Yosys::dict<vertex_t, z_matrix_t> z_matrix_index_t;

    // Private solution evaluation methods
    z_matrix_t z_in_degree_0(const vertex_d &v) const;
    z_matrix_t z_in_degree_pos(const solution_t &s, const vertex_d &v, const z_matrix_index_t &z_matrix_for) const;
    double reliability_from_z(const z_matrix_t &z) const;
};

#endif //YOSYS_ALS_EVALUATOR_H
