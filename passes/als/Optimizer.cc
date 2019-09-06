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
    const std::default_random_engine Optimizer::generator{std::random_device{}()};

    /*
     * Constructors
     */

    Optimizer::Optimizer(Yosys::Module *module, lut_catalogue_t &luts) : luts(luts), g(graph_from_module(module)) {
        // Create graph and topological ordering
        topological_sort(g, std::back_inserter(vertices));
        std::reverse(vertices.begin(), vertices.end());
    }

    /*
     * Public methods
     */

    Optimizer::solution_t Optimizer::operator()() {
        solution_t s = empty_solution();
        double t = t_0;
        size_t moved = 0; // TODO Remove when tweaking is complete
        std::uniform_real_distribution<double> chance(0.0, 1.0);

        // TODO Ensure loop iterations only evaluate once each solution
        // e.g. make dominates and accept_probability accept cost_t
        for (size_t i = 0; i < max_iter; i++) {
            auto s_tick = neighbor_of(s);

            if (!dominates(s, s_tick)) {
                s = std::move(s_tick);
            } else {
                if (chance(generator) < accept_probability(s, s_tick, t))
                    s = std::move(s_tick);
            }

            t = alpha * t;
        }

        // TODO Remove when tweaking is complete
        log("Moved: %lu\n", moved);
        log("%s %g %g\n", to_string(s).c_str(), value(s)[0], value(s)[1]);

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
                size_t decrease = std::max<size_t>(el.second - 1, 0);
                size_t increase = std::min<size_t>(el.second + 1, max);
                std::array<size_t, 2> alternatives{decrease, increase};
                s_tick[el.first] = alternatives[coin_flip(generator)];
            } else {
                s_tick[el.first] = el.second;
            }
        }

        return s_tick;
    }

    bool Optimizer::dominates(const solution_t &s1, const solution_t &s2) const {
        return cost(s1) < cost(s2);
    }

    Optimizer::value_t Optimizer::value(const solution_t &s) const {
        return value_t{circuit_reliability(output_reliability(s)), gates(s)};
    }

    Optimizer::cost_t Optimizer::cost(const solution_t &s) const {
        auto val = value(s);
        return 0.0; // TODO Determine cost function
    }

    /*
     * Review
     */

    Eigen::Matrix2d z_in_degree_0(const graph_t &g, const vertex_d &v) {
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

    Eigen::Matrix2d z_in_degree_pos(const graph_t &g, const vertex_d &v, dict<vertex_t, Eigen::Matrix2d> &z_matrix_for,
                                    const mig_model_t &exact_lut, const mig_model_t &mapped_lut) {
        // Get z matrices of the input drivers
        auto in_edges = boost::in_edges(v, g);
        std::vector<Eigen::Matrix2d> z_inputs(in_edges.second - in_edges.first);
        std::for_each(in_edges.first, in_edges.second, [&](const edge_d &e) {
            z_inputs[g[e].signal] = z_matrix_for[g[boost::source(e, g)]];
        });

        // Evaluate I matrix for the cell (std::reduce is not C++11)
        Eigen::MatrixXd big_i = z_inputs[0].replicate(1, 1);
        std::for_each(z_inputs.begin() + 1, z_inputs.end(), [&](const Eigen::Matrix2d &i) {
           big_i = Eigen::kroneckerProduct(big_i, i).eval();
        });

        // Evaluate ITM and PTM for the cell
        if (exact_lut.fun_spec.size() != mapped_lut.fun_spec.size())
            throw std::runtime_error("Exact and mapped LUT have different input size");
        Eigen::Matrix<bool, Eigen::Dynamic, Eigen::Dynamic> itm(exact_lut.fun_spec.size(), 2);
        Eigen::Matrix<bool, Eigen::Dynamic, Eigen::Dynamic> ptm(mapped_lut.fun_spec.size(), 2);
        for (size_t i = 0; i < exact_lut.fun_spec.size(); i++) {
            itm(i, 0) = !exact_lut.fun_spec[i];
            itm(i, 1) = exact_lut.fun_spec[i];
            ptm(i, 0) = !mapped_lut.fun_spec[i];
            ptm(i, 1) = mapped_lut.fun_spec[i];
        }

        // Evaluate output probability according to PTM
        Eigen::MatrixXd big_p = big_i * ptm.cast<double>();

        // Sum contributes according to ITM
        Eigen::Matrix2d z_matrix = Eigen::Matrix2d::Zero();
        for (size_t i = 0; i < exact_lut.fun_spec.size(); i++) {
            if (itm(i, 0)) {
                z_matrix(0, 0) += big_p(i, 0);
                z_matrix(1, 0) += big_p(i, 1);
            } else {
                z_matrix(1, 1) += big_p(i, 1);
                z_matrix(0, 1) += big_p(i, 0);
            }
        }

        return z_matrix;
    }

    double reliability_from_z(const Eigen::Matrix2d &z) {
        return z(0, 0) + z(1, 1);
    }

    /*
     * Exposed functions
     */

    dict<IdString, double> output_reliability(const graph_t &g, const std::vector<vertex_d> &topological_order,
                                              dict<Const, std::vector<mig_model_t>> &synthesized_luts, dict<vertex_t, size_t> &mapping) {
        dict<IdString, double> rel;
        dict<vertex_t, Eigen::Matrix2d> z_matrix_for;

        for (auto &v : topological_order) {
            if (boost::in_degree(v, g) == 0) {
                z_matrix_for[g[v]] = z_in_degree_0(g, v);
            } else { // Other vertices (i.e. cells)
                auto &cell_function = get_lut_param(g[v].cell);
                auto &lut = synthesized_luts[cell_function];
                z_matrix_for[g[v]] = z_in_degree_pos(g, v, z_matrix_for, lut[0], lut[mapping[g[v]]]);

                // Cells connected to primary outputs
                if (boost::out_degree(v, g) == 0)
                    rel[g[v].name] = reliability_from_z(z_matrix_for[g[v]]);
            }
        }

        return rel;
    }

    double circuit_reliability(const dict<IdString, double> &all_the_rels) {
        double c_rel = 1.0;

        // FIXME Don't assume all cells go to only one output
        for (auto &a_rel : all_the_rels)
            c_rel *= a_rel.second;

        return c_rel;
    }

    double gates_ratio(dict<Const, std::vector<mig_model_t>> &luts, dict<vertex_t, size_t> &sol) {
        size_t count = 0;
        size_t baseline = 0;

        for (auto const &v : sol) {
            baseline += luts[get_lut_param(v.first.cell)][0].num_gates;
            count += luts[get_lut_param(v.first.cell)][v.second].num_gates;
        }

        return static_cast<double>(count) / baseline;
    }

    double accept_probability(const std::array<double, 2> &eval1, const std::array<double, 2> &eval2, const double temp) {
        // TODO Evaluate this
        //double cost = ((eval2[0] - eval1[0]) + (eval2[1] - eval2[0])) / 2;
        auto ind1 = 0.7 * eval1[0] + 0.3 * eval1[1];
        auto ind2 = 0.7 * eval2[0] + 0.3 * eval2[1];
        double cost = ind1 - ind2;
        double prob = exp(-cost/temp);

        return prob > 0 ? prob : 0;
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
