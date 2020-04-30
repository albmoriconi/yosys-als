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

#include "Evaluator.h"

/*
 * Private solution evaluation methods
 */

double Optimizer::circuit_reliability(const reliability_index_t &all_the_rels) const {
    double c_rel = 1.0;

    for (auto &a_rel : all_the_rels) {
        auto cell = a_rel.first.cell;

        for (auto &conn : cell->connections()) {
            if (cell->output(conn.first))
                for (auto &bit : sigmap(conn.second)) {
                    if (weights.find(bit) != weights.end())
                        c_rel *= std::pow(a_rel.second, weights[bit]);
                    else
                        c_rel *= a_rel.second;
                }
        }
    }

    return c_rel; //std::pow(c_rel, 1.0 / rel_norm);
}

Optimizer::reliability_index_t Optimizer::output_reliability(const solution_t &s) const {
    reliability_index_t rel;
    z_matrix_index_t z_matrix_for;

    for (auto &v : vertices) {
        if (boost::in_degree(v, g) == 0) {
            z_matrix_for[g[v]] = z_in_degree_0(v);
        } else { // Other vertices (i.e. cells)
            z_matrix_for[g[v]] = z_in_degree_pos(s, v, z_matrix_for);

            // Cells connected to primary outputs
            if (boost::out_degree(v, g) == 0)
                rel[g[v]] = reliability_from_z(z_matrix_for[g[v]]);
        }
    }

    return rel;
}

Optimizer::z_matrix_t Optimizer::z_in_degree_0(const vertex_d &v) const {
    z_matrix_t z_matrix;

    switch (g[v].type) {
        case vertex_t::CONSTANT_ZERO:
            z_matrix << 1.0, 0.0, 0.0, 0.0;
            break;
        case vertex_t::CONSTANT_ONE:
            z_matrix << 0.0, 0.0, 0.0, 1.0;
            break;
        case vertex_t::PRIMARY_INPUT:
            z_matrix << 0.5, 0.0, 0.0, 0.5;
            break;
        default:
            throw std::runtime_error("Bad vertex " + g[v].name.str());
    }

    return z_matrix;
}

Optimizer::z_matrix_t Optimizer::z_in_degree_pos(const solution_t &s, const vertex_d &v,
                                                 const z_matrix_index_t &z_matrix_for) const {
    // Get exact and chosen LUT for vertex
    auto &cell_function = get_lut_param(g[v].cell);
    auto &lut = luts[cell_function];
    auto &exact_lut = lut[0];
    auto &chosen_lut = lut[s.at(g[v])];

    // Get z matrices of the input drivers
    auto in_edges = boost::in_edges(v, g);
    std::vector<z_matrix_t> z_inputs(in_edges.second - in_edges.first);
    std::for_each(in_edges.first, in_edges.second, [&](const edge_d &e) {
        z_inputs[g[e].signal] = z_matrix_for.at(g[boost::source(e, g)]);
    });

    // Evaluate I matrix for the cell
    matrix_double_t big_i = z_inputs[0].replicate(1, 1);
    std::for_each(z_inputs.begin() + 1, z_inputs.end(), [&](const z_matrix_t &z) {
        big_i = Eigen::kroneckerProduct(big_i, z).eval();
    });

    // Evaluate ITM and PTM for the cell
    if (exact_lut.fun_spec.size() != chosen_lut.fun_spec.size())
        throw std::runtime_error("Exact and mapped LUT have different input size");
    matrix_bool_t itm(exact_lut.fun_spec.size(), 2);
    matrix_bool_t ptm(chosen_lut.fun_spec.size(), 2);
    for (size_t i = 0; i < exact_lut.fun_spec.size(); i++) {
        itm(i, 0) = !exact_lut.fun_spec[i];
        itm(i, 1) = exact_lut.fun_spec[i];
        ptm(i, 0) = !chosen_lut.fun_spec[i];
        ptm(i, 1) = chosen_lut.fun_spec[i];
    }

    // Evaluate output probability according to PTM, sum contributes according to ITM
    z_matrix_t z_matrix = (big_i * ptm.cast<double>()).transpose() * itm.cast<double>();

    return z_matrix;
}

double Optimizer::reliability_from_z(const z_matrix_t &z) const {
    return z(0, 0) + z(1, 1);
}

size_t Optimizer::gates(const solution_t &s) const {
    size_t count = 0;

    for (auto &v : s)
        count += luts[get_lut_param(v.first.cell)][v.second].num_gates;

    return count;
}
