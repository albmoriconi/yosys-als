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
 * @brief Optimization utility functions for Yosys ALS module
 */

#ifndef YOSYS_ALS_OPTIMIZER_UTILS_H
#define YOSYS_ALS_OPTIMIZER_UTILS_H

#include "graph.h"
#include "smtsynth.h"
#include "kernel/yosys.h"

#include <Eigen/Dense>

namespace yosys_als {

    Eigen::Matrix2d z_in_degree_0(const Graph &g, const Vertex &v);

    Eigen::Matrix2d z_in_degree_pos(const Graph &g, const Vertex &v, Yosys::dict<vertex_t, Eigen::Matrix2d> &z_matrix_for,
                                    const mig_model_t &exact_lut, const mig_model_t &mapped_lut);

    double reliability_from_z(const Eigen::Matrix2d &z);
}

#endif //YOSYS_ALS_OPTIMIZER_UTILS_H
