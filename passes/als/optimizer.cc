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
#include <unsupported/Eigen/KroneckerProduct>

USING_YOSYS_NAMESPACE

namespace yosys_als {

    /*
     * Utility functions
     */

    Eigen::Matrix2d z_in_degree_0(const Graph &g, const Vertex &v) {
        Eigen::Matrix2d z_matrix;

        switch (g[v].type) {
            case vertex_t::CONSTANT_ZERO:
                z_matrix(0, 0) = 1.0;
                z_matrix(0, 1) = 0.0;
                z_matrix(1, 0) = 0.0;
                z_matrix(1, 1) = 0.0;
                break;
            case vertex_t::CONSTANT_ONE:
                z_matrix(0, 0) = 0.0;
                z_matrix(0, 1) = 0.0;
                z_matrix(1, 0) = 0.0;
                z_matrix(1, 1) = 1.0;
                break;
            case vertex_t::PRIMARY_INPUT:
                z_matrix(0, 0) = 0.5;
                z_matrix(0, 1) = 0.0;
                z_matrix(1, 0) = 0.0;
                z_matrix(1, 1) = 0.5;
                break;
            default:
                throw std::runtime_error("Bad vertex " + g[v].name.str());
        }

        return z_matrix;
    }

    Eigen::Matrix2d z_in_degree_pos(const Graph &g, const Vertex &v, dict<vertex_t, Eigen::Matrix2d> &z_matrix_for) {
        Eigen::Matrix2d z_matrix;

        // Get z matrices of the input drivers
        auto in_edges = boost::in_edges(v, g);
        std::vector<Eigen::Matrix2d> z_inputs(in_edges.second - in_edges.first);
        std::for_each(in_edges.first, in_edges.second, [&](const Edge &e) {
            z_inputs[g[e].signal] = z_matrix_for[g[boost::source(e, g)]];
        });

        // Evaluate I matrix for the cell
        Eigen::MatrixXd big_i = z_inputs[0].replicate(1, 1);
        std::for_each(z_inputs.begin() + 1, z_inputs.end(), [&](const Eigen::Matrix2d &i) {
           big_i = Eigen::kroneckerProduct(big_i, i).eval();
        });

        std::cout << big_i << "\n\n";

        z_matrix(0, 0) = 0.0;
        z_matrix(0, 1) = 0.0;
        z_matrix(1, 0) = 0.0;
        z_matrix(1, 1) = 0.0;

        return z_matrix;
    }

    double reliability_from_z(const Eigen::Matrix2d &z) {
        return z(0, 0) + z(1, 1);
    }

    /*
     * Exposed functions
     */

    dict<IdString, double> output_reliability(const Graph &g, const std::vector<Vertex> &topological_order,
            const dict<Const, std::vector<mig_model_t>> &synthesized_luts, const std::vector<size_t> &mapping) {
        dict<IdString, double> rel;
        dict<vertex_t, Eigen::Matrix2d> z_matrix_for;

        for (auto const &v : boost::adaptors::reverse(topological_order) | boost::adaptors::indexed(0)) {
            // Primary inputs and constants
            if (boost::in_degree(v.value(), g) == 0) {
                z_matrix_for[g[v.value()]] = z_in_degree_0(g, v.value());
            } else { // Other vertices (i.e. cells)
                z_matrix_for[g[v.value()]] = z_in_degree_pos(g, v.value(), z_matrix_for);

                // Cells connected to primary outputs
                if (boost::out_degree(v.value(), g) == 0)
                    rel[g[v.value()].name] = reliability_from_z(z_matrix_for[g[v.value()]]);
            }
        }

        return rel;
    }
}
