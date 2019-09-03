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

// [[CITE]] Signal probability for reliability evaluation of logic circuits
// Denis Teixeira Franco, Maí Correia Vasconcelos, Lirida Naviner, and Jean-François Naviner

/**
 * @file
 * @brief Optimization utility functions for Yosys ALS module
 */

#include "optimizer.h"

#include "graph.h"
#include "smtsynth.h"
#include "kernel/yosys.h"

#include <boost/range/adaptor/indexed.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <Eigen/Dense>

USING_YOSYS_NAMESPACE

namespace yosys_als {

    dict<IdString, double> output_reliability(const Graph &g, const std::vector<Vertex> &topological_order,
            const dict<Const, std::vector<mig_model_t>> &synthesized_luts, const std::vector<size_t> &mapping) {
        dict<IdString, double> rel;
        dict<IdString, Eigen::Matrix2d> z_matrix_for;

        for (auto const &v : boost::adaptors::reverse(topological_order) | boost::adaptors::indexed(0)) {
            std::cout << "Node: " << v.index() << " IdString: " << g[v.value()].name.c_str() << "\n";

            if (boost::in_degree(v.value(), g) == 0) {
                std::cout << "Input\n";
            }

            if (boost::out_degree(v.value(), g) == 0)
                std::cout << "Output\n";
        }

        return rel;
    }
}
