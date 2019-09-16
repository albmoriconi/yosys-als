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

// [[CITE]] A Simulated Annealing Based Multi-objective Optimization Algorithm: AMOSA
// Sanghamitra Bandyopadhyay, Sriparna Saha, Ujjwal Maulik, and Kalyanmoy Deb

/**
 * @file
 * @brief Optimization utility functions for Yosys ALS module
 */

#include "Optimizer.h"

#include "graph.h"
#include "yosys_utils.h"
#include "kernel/yosys.h"

#include <boost/graph/topological_sort.hpp>
#include <unsupported/Eigen/KroneckerProduct>

#include <cmath>
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

    Optimizer::Optimizer(Module *module, weights_t &weights, lut_catalogue_t &luts)
    : g(graph_from_module(module)), sigmap(module), weights(weights), luts(luts) {
        // Create graph and topological ordering
        topological_sort(g, std::back_inserter(vertices));
        std::reverse(vertices.begin(), vertices.end());
        // Count reliability normalization factor
        rel_norm = 0.0;
        for (auto &w : weights)
            rel_norm += w.second;
        // Count gates baseline
        gates_baseline = gates(empty_solution().first);
    }

    /*
     * Public methods
     */

    Optimizer::solution_t Optimizer::operator()() {
        // Populate starting archive
        archive_t arch;
        for (size_t i = 0; i < soft_limit; i++) {
            // Do a "biased sweep" of the front to augment diversity of initial archive
            archive_entry_t s = hill_climb(empty_solution(), static_cast<double>(i) / soft_limit);
            if (std::find(arch.begin(), arch.end(), s) == arch.end())
                arch.push_back(s);
        }

        erase_dominated(arch);
        //log("First glimpse:\n");
        //for (auto &sol : arch) {
        //    log("%s %g %g\n", to_string(sol.first).c_str(), sol.second[0], sol.second[1]);
        //}

        double t = t_max;
        size_t moved = 0; // TODO Remove when tweaking is complete
        size_t tried = 0;
        std::uniform_real_distribution<double> chance(0.0, 1.0);
        auto s_curr = arch[0]; // TODO Should we choose randomly and follow AMOSA?

        for (size_t i = 0; i < max_iter; i++) { // TODO Try a temperature scheduling approach
            auto s_tick = neighbor_of(s_curr);

            if (dominates(s_curr, s_tick)) {
                double delta_tot = delta_dom(s_curr, s_tick);
                size_t k = 1;
                for (auto &s : arch) {
                    if (dominates(s, s_tick)) {
                        delta_tot += delta_dom(s, s_tick);
                        k++;
                    }
                }

                tried++;
                if (chance(generator) < accept_probability(delta_tot / k, t)) {
                    s_curr = std::move(s_tick);
                    moved++;
                }
            } else if (dominates(s_tick, s_curr)) {
                std::vector<double> delta_doms;
                for (auto &s : arch) {
                    if (dominates(s, s_tick))
                        delta_doms.push_back(delta_dom(s, s_tick));
                }

                if (!delta_doms.empty()) {
                    tried++;
                    double delta_min = *std::min_element(delta_doms.begin(), delta_doms.end());
                    if (chance(generator) < accept_probability(-delta_min, 1)) {
                        moved++;
                        s_curr = std::move(s_tick);
                    }
                } else {
                    s_curr = std::move(s_tick);
                    if (std::find(arch.begin(), arch.end(), s_curr) == arch.end())
                        arch.push_back(s_curr);
                    erase_dominated(arch);
                }
            } else {
                double delta_tot = 0.0;
                size_t k = 0;
                for (auto &s : arch) {
                    if (dominates(s, s_tick)) {
                        delta_tot += delta_dom(s, s_tick);
                        k++;
                    }
                }

                if (k > 0) {
                    tried++;
                    if (chance(generator) < accept_probability(delta_tot / k, t)) {
                        s_curr = std::move(s_tick);
                        moved++;
                    }
                } else {
                    s_curr = std::move(s_tick);
                    if (std::find(arch.begin(), arch.end(), s_curr) == arch.end())
                        arch.push_back(s_curr);
                    erase_dominated(arch);
                }
            }

            t = cooling * t;
        }

        // TODO Remove when tweaking is complete
        //log("Moved: %lu/%lu\n", moved, tried);
        //log("Final archive:\n");
        std::sort(arch.begin(), arch.end(), [](const archive_entry_t &a, const archive_entry_t &b) {
            return a.second[0] < b.second[0];
        });
        print_archive(arch);
        //for (auto &sol : arch) {
        //    log("%s %g %g\n", to_string(sol.first).c_str(), sol.second[0], sol.second[1]);
        //}

        size_t choice;
        std::cout << "Please select an entry:\n";
        std::cin >> choice;
        return arch[choice].first;
    }

    void Optimizer::erase_dominated(Optimizer::archive_t &arch) const {
        arch.erase(std::remove_if(arch.begin(), arch.end(), [&](const archive_entry_t &s_tick) {
            for (auto &s : arch) {
                if (dominates(s, s_tick))
                    return true;
            }
            return false;
        }), arch.end());
    }

    std::string Optimizer::to_string(const solution_t &s) const {
        std::string str;

        for (auto &v : vertices) {
            if (g[v].type == vertex_t::CELL)
                str += static_cast<char>(s.at(g[v])) + '0';
        }

        return str;
    }

    /*
     * Utility and private methods
     */

    Optimizer::archive_entry_t Optimizer::empty_solution() const {
        solution_t s;

        for (auto &v : vertices) {
            if (g[v].type == vertex_t::CELL)
                s[g[v]] = 0;
        }

        return {s, value(s)};
    }

    Optimizer::archive_entry_t Optimizer::hill_climb(const archive_entry_t &s, const double arel_bias) const {
        auto s_climb = s;

        for (size_t i = 0; i < max_iter; i++) {
            auto s_tick = neighbor_of(s_climb);
            if (dominates(s_tick, s_climb, arel_bias))
                s_climb = std::move(s_tick);
        }

        return s_climb;
    }

    Optimizer::archive_entry_t Optimizer::neighbor_of(const archive_entry_t &s) const {
        solution_t s_tick;
        std::uniform_int_distribution<size_t> pos_dist(0, s.first.size() - 1);
        std::uniform_int_distribution<size_t> coin_flip(0, 1);
        size_t target = pos_dist(generator);

        // Move up or down a random element of the solution
        for (auto &el : s.first) {
            if (s_tick.size() == s.first.size() - target - 1) {
                size_t max = luts[get_lut_param(el.first.cell)].size() - 1;
                if (max == 0) {
                    s_tick[el.first] = 0;
                } else {
                    size_t decrease = el.second > 0 ? el.second - 1 : el.second + 1;
                    size_t increase = el.second < max ? el.second + 1 : el.second - 1;
                    s_tick[el.first] = coin_flip(generator) == 1 ? increase : decrease;
                }
            } else {
                s_tick[el.first] = el.second;
            }
        }

        return {s_tick, value(s_tick)};
    }

    bool Optimizer::dominates(const archive_entry_t &s1, const archive_entry_t &s2, const double arel_bias) const {
        double arel1 = fabs(arel_bias - s1.second[0]);
        double arel2 = fabs(arel_bias - s2.second[0]);
        double gate1 = s1.second[1];
        double gate2 = s2.second[1];

        return (arel1 <= arel2 && gate1 < gate2) || (arel1 < arel2 && gate1 <= gate2);
    }

    Optimizer::value_t Optimizer::value(const solution_t &s) const {
        return value_t{1 - circuit_reliability(output_reliability(s)),
                       static_cast<double>(gates(s)) / gates_baseline};
    }

    void Optimizer::print_archive(const archive_t &arch) const {
        log(" Solution archive\n");
        log(" Entry     Chosen LUTs         Arel        Gates\n");
        log(" ----- --------------- ------------ ------------\n");

        for (size_t i = 0; i < arch.size(); i++) {
            auto choice_s = to_string(arch[i].first);
            if (choice_s.size() > 15)
                choice_s = choice_s.substr(0, 15);
            log(" %5zu %15s %12g %12g\n", i, choice_s.c_str(), arch[i].second[0], arch[i].second[1]);
        }
    }

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