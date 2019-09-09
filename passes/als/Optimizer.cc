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

#include "Optimizer.h"

#include "graph.h"
#include "smtsynth.h"
#include "yosys_utils.h"
#include "kernel/yosys.h"

#include <boost/graph/topological_sort.hpp>
#include <Eigen/Dense>
#include <unsupported/Eigen/KroneckerProduct>

#include <random>

USING_YOSYS_NAMESPACE

namespace yosys_als {

    /*
     * Static members
     */

    std::default_random_engine Optimizer::generator{std::random_device{}()};

    /*
     * Constructors
     */

    Optimizer::Optimizer(Module *module, lut_catalogue_t &luts) : g(graph_from_module(module)), luts(luts) {
        // Create graph and topological ordering
        topological_sort(g, std::back_inserter(vertices));
        std::reverse(vertices.begin(), vertices.end());
        // Count gates baseline
        gates_baseline = gates(empty_solution());
    }

    /*
     * Public methods
     */

    Optimizer::solution_t Optimizer::operator()() {
        solution_t s = empty_solution();
        double t = t_0;
        size_t moved = 0; // TODO Remove when tweaking is complete
        std::uniform_real_distribution<double> chance(0.0, 1.0);

        for (size_t i = 0; i < max_iter; i++) {
            auto s_tick = neighbor_of(s);
            auto c_s = cost(value(s));
            auto c_s_tick = cost(value(s_tick));

            if (!dominates(c_s, c_s_tick)) {
                s = std::move(s_tick);
            } else {
                if (chance(generator) < accept_probability(c_s, c_s_tick, t)) {
                    s = std::move(s_tick);
                    moved++;
                }
            }

            t = alpha * t;
        }

        // TODO Remove when tweaking is complete
        log("Moved: %lu\n", moved);
        log("%s %g %lu\n", to_string(s).c_str(), value(s).first, value(s).second);

        return s;
    }

    std::string Optimizer::to_string(solution_t &s) const {
        std::string str;

        for (auto &v : vertices) {
            if (g[v].type == vertex_t::CELL)
                str += static_cast<char>(s[g[v]]) + '0';
        }

        return str;
    }

    /*
     * Utility and private methods
     */

    Optimizer::solution_t Optimizer::empty_solution() const {
        solution_t s;

        for (auto &v : vertices) {
            if (g[v].type == vertex_t::CELL)
                s[g[v]] = 0;
        }

        return s;
    }

    Optimizer::solution_t Optimizer::neighbor_of(const solution_t &s) const {
        solution_t s_tick;
        std::uniform_int_distribution<size_t> pos_dist(0, s.size() - 1);
        std::uniform_int_distribution<size_t> coin_flip(0, 1);
        size_t target = pos_dist(generator);

        // Move up or down a random element of the solution
        for (auto &el : s) {
            if (s_tick.size() == s.size() - target - 1) {
                size_t max = luts[get_lut_param(el.first.cell)].size() - 1;
                size_t decrease = el.second > 0 ? el.second - 1 : 0;
                size_t increase = el.second < max ? el.second + 1 : max;
                s_tick[el.first] = coin_flip(generator) == 1 ? increase : decrease;
            } else {
                s_tick[el.first] = el.second;
            }
        }

        return s_tick;
    }

    bool Optimizer::dominates(const cost_t &c1, const cost_t &c2) const {
        return c1 < c2;
    }

    Optimizer::value_t Optimizer::value(const solution_t &s) const {
        return value_t{circuit_reliability(output_reliability(s)), gates(s)};
    }

    Optimizer::cost_t Optimizer::cost(const value_t &v) const {
        return 0.7 * fabs(0.2 - (1.0 - v.first)) + 0.3 * static_cast<double>(v.second) / gates_baseline;
    }

    double Optimizer::accept_probability(const cost_t &c1, const cost_t &c2, double temp) {
        double cost = c1 - c2;
        double prob = std::min<double>(std::max<double>(exp(-cost/temp), 0.0), 1.0);

        return prob;
    }

    /*
     * Private solution evaluation methods
     */

    double Optimizer::circuit_reliability(const reliability_index_t &all_the_rels) const {
        double c_rel = 1.0;

        // FIXME Don't assume all cells go to only one output
        for (auto &a_rel : all_the_rels)
            c_rel *= a_rel.second;

        return c_rel;
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
                    rel[g[v].name] = reliability_from_z(z_matrix_for[g[v]]);
            }
        }

        return rel;
    }

    Optimizer::z_matrix_t Optimizer::z_in_degree_0(const vertex_d &v) const {
        Eigen::Matrix2d z_matrix;

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
}

//if (i % 10 == 0 || i == iter-1) {
//size_t curr = floor(31 * static_cast<double>(i) / iter);
//std::cerr << "\r     |";
//for (size_t j = 0; j < curr; j++)
//std::cerr << "█";
//if (i != iter-1)
//for (size_t j = curr; j < 31; j++)
//std::cerr << " ";
//if (i == iter-1 && i % 10 != 0)
//std::cerr << "█";
//std::cerr << "|" << std::flush;
//if (i == iter-1 && i % 10 != 0)
//std::cerr << "\n";
//}
