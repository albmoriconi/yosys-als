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

// [[CITE]] Multiobjective Simulated Annealing: A Comparative Study to Evolutionary Algorithms
// Dongkyung Nam, and Cheol Hoon Park

/**
 * @file
 * @brief Optimization utility functions for Yosys ALS module
 */

#include "optimizer.h"

#include "graph.h"
#include "smtsynth.h"
#include "yosys_utils.h"
#include "kernel/yosys.h"

#include <boost/graph/topological_sort.hpp>
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

    Eigen::Matrix2d z_in_degree_pos(const Graph &g, const Vertex &v, dict<vertex_t, Eigen::Matrix2d> &z_matrix_for,
            const mig_model_t &exact_lut, const mig_model_t &mapped_lut) {
        // Get z matrices of the input drivers
        auto in_edges = boost::in_edges(v, g);
        std::vector<Eigen::Matrix2d> z_inputs(in_edges.second - in_edges.first);
        std::for_each(in_edges.first, in_edges.second, [&](const Edge &e) {
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

    dict<IdString, double> output_reliability(const Graph &g, const std::vector<Vertex> &topological_order,
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

        return static_cast<float>(count) / baseline;
    }

    std::array<double, 2> evaluation_function(const Graph &g, const std::vector<Vertex> &topological_order,
            dict<Const, std::vector<mig_model_t>> &synthesized_luts, dict<vertex_t, size_t> &sol) {
        return std::array<double, 2>
            {1.0 - circuit_reliability(output_reliability(g, topological_order, synthesized_luts, sol)),
             gates_ratio(synthesized_luts, sol)};
    }

    dict<vertex_t, size_t> neighbor_of(dict<vertex_t, size_t> &sol, dict<Const, std::vector<mig_model_t>> &luts) {
        dict<vertex_t, size_t> neighbor;
        auto pos = rand() % sol.size();

        for (auto &gene : sol) {
            if (neighbor.size() == sol.size() - pos - 1) {
                // TODO Should we take at random or only move by one?
                size_t max = luts[get_lut_param(gene.first.cell)].size() - 1;

                if (gene.second == max) {
                    if (gene.second != 0)
                        neighbor[gene.first] = gene.second - 1;
                } else if (gene.second == 0) {
                    if (gene.second != max)
                        neighbor[gene.first] = gene.second + 1;
                } else {
                    auto chance = rand() % 2;
                    if (chance == 0)
                        neighbor[gene.first] = gene.second - 1;
                    else
                        neighbor[gene.first] = gene.second + 1;
                }
            }
            else
                neighbor[gene.first] = gene.second;
        }

        return neighbor;
    }

    std::string sol_string(const Graph &g, dict<vertex_t, size_t> &sol, const std::vector<Vertex> &order) {
        std::string s;

        for (auto &v : order) {
            if (g[v].type == vertex_t::CELL)
                s += sol[g[v]] + 48;
        }

        return s;
    }

    int sol_dominates(const Graph &g, const std::vector<Vertex> &topological_order,
            dict<Const, std::vector<mig_model_t>> &synthesized_luts,
            dict<vertex_t, size_t> &sol1, dict<vertex_t, size_t> &sol2) {
        auto eval1 = evaluation_function(g, topological_order, synthesized_luts, sol1);
        auto eval2 = evaluation_function(g, topological_order, synthesized_luts, sol2);

        // TODO We have to think this thoroughly - for now use a single indicator
        auto ind1 = 0.7 * eval1[0] + 0.3 * eval1[1];
        auto ind2 = 0.7 * eval2[0] + 0.3 * eval2[1];

        if (ind1 < ind2)
            return 1;

        return -1;
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

    std::vector<dict<vertex_t, size_t>> optimizer_mosa(Module *const module,
            dict<Const, std::vector<mig_model_t>> &synthesized_luts) {
        // Parameters
        constexpr double alpha = 0.9;
        double temp = 25; // Prova temp 5*luts, it 4 * temp

        // Create graph and topological ordering
        Graph g = graph_from_module(module);
        std::vector<Vertex> topological_order;
        topological_sort(g, std::back_inserter(topological_order));
        std::reverse(topological_order.begin(), topological_order.end());

        // Create first solution
        std::vector<dict<vertex_t, size_t>> hall_of_fame;
        dict<vertex_t, size_t> sol;
        srand(time(nullptr));
        for (auto &v : topological_order) {
            if (g[v].type == vertex_t::CELL)
                sol[g[v]] = 0; // rand() % synthesized_luts[get_lut_param(g[v].cell)].size();
        }

        auto eval = evaluation_function(g, topological_order, synthesized_luts, sol);
        log("%s %g %g\n", sol_string(g, sol, topological_order).c_str(), eval[0], eval[1]);

        hall_of_fame.push_back(sol);
        size_t moved = 0;
        for (size_t i = 0; i < 100; i++) {
            auto new_sol = neighbor_of(hall_of_fame.back(), synthesized_luts);

            auto dom = sol_dominates(g, topological_order, synthesized_luts, new_sol, hall_of_fame.back());
            if (dom != -1)
                hall_of_fame.push_back(new_sol);
            else {
                auto eval1 = evaluation_function(g, topological_order, synthesized_luts, new_sol);
                auto eval2 = evaluation_function(g, topological_order, synthesized_luts, hall_of_fame.back());
                double prob = accept_probability(eval1, eval2, temp);
                double chance = (double) rand() / RAND_MAX;
                if (chance < prob) {
                    hall_of_fame.push_back(new_sol);
                    moved++;
                }
            }

            temp = alpha * temp;
        }

        log("Moved: %lu\n", moved);
        eval = evaluation_function(g, topological_order, synthesized_luts, hall_of_fame.back());
        log("%s %g %g\n", sol_string(g, hall_of_fame.back(), topological_order).c_str(), eval[0], eval[1]);
        return hall_of_fame;
    }
}
