/* -*- c++ -*-
 *  yosys-als -- Approximate logic synthesis
 *
 *  Copyright (C) 2019  Alberto Moriconi <albmoriconi@gmail.com>
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

// [[CITE]] A Simulated Annealing Based Multi-objective Optimization Algorithm: AMOSA
// Sanghamitra Bandyopadhyay, Sriparna Saha, Ujjwal Maulik, and Kalyanmoy Deb

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

#include <boost/graph/topological_sort.hpp>

#include <random>

namespace yosys_als {

    extern std::default_random_engine rng;

    // Forward declaration
    template<typename E> class Optimizer;

    /// Type for a solution
    typedef Yosys::dict<vertex_t, size_t> solution_t;

    /// Type for an entry in the solution archive
    template<typename E>
    using archive_entry_t = std::pair<solution_t, typename E::value_t>;

    /// Type for the archive of solutions
    template<typename E>
    using archive_t = std::vector<archive_entry_t<E>>;

    /// Type for the input weights
    typedef Yosys::dict<Yosys::SigBit, double> weights_t;

    /**
     * @brief Type for the optimizer context
     */
    template<typename E>
    struct optimizer_context_t {
        Optimizer<E> *opt;
        Graph &g;
        std::vector<vertex_d> &vertices;
        Yosys::SigMap &sigmap;
        weights_t &weights;
        lut_catalogue_t &luts;
    };

    /**
     * @brief Implements the optimization heuristic for ALS
     * @tparam E A type that implements evaluation primitives for solutions
     */
    template<typename E>
    class Optimizer {
    public:
        /**
         * @brief Constructs an optimizer
         * @param module A module
         * @param luts The lut catalogue for the model
         */
        Optimizer(Yosys::Module *module, weights_t &weights, lut_catalogue_t &luts)
                : g(graph_from_module(module)), sigmap(module), weights(weights), luts(luts),
                ctx(optimizer_context_t<E> {this, g, vertices, sigmap, weights, luts}), evaluator(&ctx) { }

        /**
         * @brief Setup the evaluator
         * @note This way we complete construction before initialization
         */
        void setup() {
            // Create topological ordering for graph
            topological_sort(g.g, std::back_inserter(vertices));
            std::reverse(vertices.begin(), vertices.end());

            // Setup the evaluator
            evaluator.setup();
        }

        /**
         * @brief Executes the heuristic optimization
         * @return A local optimum for the problem
         */
        archive_t<E> operator()() {
            // Populate starting archive
            archive_t<E> arch;
            for (size_t i = 0; i < soft_limit; i++) {
                // Do a "biased sweep" of the front to augment diversity of initial archive
                archive_entry_t<E> s =
                        hill_climb(empty_solution(), static_cast<double>(i) / soft_limit);
                if (std::find(arch.begin(), arch.end(), s) == arch.end())
                    arch.push_back(s);
            }

            erase_dominated(arch);

            double t = t_max;
            std::uniform_real_distribution<double> chance(0.0, 1.0);
            auto s_curr = arch[0]; // TODO Should we choose randomly and follow AMOSA?

            for (size_t i = 0; i < max_iter; i++) { // TODO Try a temperature scheduling approach
                auto s_tick = neighbor_of(s_curr);

                if (evaluator.dominates(s_curr, s_tick)) {
                    double delta_tot = evaluator.delta_dom(s_curr, s_tick);
                    size_t k = 1;
                    for (auto &s : arch) {
                        if (evaluator.dominates(s, s_tick)) {
                            delta_tot += evaluator.delta_dom(s, s_tick);
                            k++;
                        }
                    }

                    if (chance(rng) < accept_probability(delta_tot / k, t))
                        s_curr = std::move(s_tick);
                } else if (evaluator.dominates(s_tick, s_curr)) {
                    std::vector<double> delta_doms;
                    for (auto &s : arch) {
                        if (evaluator.dominates(s, s_tick))
                            delta_doms.push_back(evaluator.delta_dom(s, s_tick));
                    }

                    if (!delta_doms.empty()) {
                        double delta_min = *std::min_element(delta_doms.begin(), delta_doms.end());
                        if (chance(rng) < accept_probability(-delta_min, 1))
                            s_curr = std::move(s_tick);
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
                        if (evaluator.dominates(s, s_tick)) {
                            delta_tot += evaluator.delta_dom(s, s_tick);
                            k++;
                        }
                    }

                    if (k > 0) {
                        if (chance(rng) < accept_probability(delta_tot / k, t))
                            s_curr = std::move(s_tick);
                    } else {
                        s_curr = std::move(s_tick);
                        if (std::find(arch.begin(), arch.end(), s_curr) == arch.end())
                            arch.push_back(s_curr);
                        erase_dominated(arch);
                    }
                }

                t = cooling * t;
            }

            std::sort(arch.begin(), arch.end(), [](const archive_entry_t<E> &a, const archive_entry_t<E> &b) {
                return a.second[0] < b.second[0];
            });

            return arch;
        }

        /**
         * @brief Returns an empty solution
         */
        archive_entry_t<E> empty_solution() const {
            solution_t s;

            for (auto &v : vertices) {
                if (g.g[v].type == vertex_t::CELL)
                    s[g.g[v]] = 0;
            }

            return {s, evaluator.value(s)};
        }

        /**
         * Converts a solution to a string
         * @param s A solution
         * @return A string representation of the solution
         */
        std::string to_string(const solution_t &s) const {
            std::string str;

            for (auto &v : vertices) {
                if (g.g[v].type == vertex_t::CELL)
                    str += static_cast<char>(s.at(g.g[v])) + '0';
            }

            return str;
        }

    private:
        // Private data (some are duplicated because we own them)
        Graph g;
        std::vector<vertex_d> vertices;
        Yosys::SigMap sigmap;
        weights_t &weights;
        lut_catalogue_t &luts;

        // The context
        optimizer_context_t<E> ctx;

        // The solution evaluator
        E evaluator;

        // Parameters
        // TODO Tweak parameters (e.g. temp = 5*luts, iter = 4*temp, ...)
        static constexpr size_t soft_limit = 20;
        static constexpr double t_max = 1500;
        static constexpr double t_min = 0.01;
        static constexpr double cooling = 0.9;
        static constexpr size_t max_iter = 2500;

        // Private methods
        archive_entry_t<E> hill_climb(const archive_entry_t<E> &s, double arel_bias = 0.0) const {
            auto s_climb = s;

            for (size_t i = 0; i < max_iter/10; i++) {
                auto s_tick = neighbor_of(s_climb);
                if (evaluator.dominates(s_tick, s_climb, arel_bias))
                    s_climb = std::move(s_tick);
            }

            return s_climb;
        }

        archive_entry_t<E> neighbor_of(const archive_entry_t<E> &s) const {
            solution_t s_tick;
            std::uniform_int_distribution<size_t> pos_dist(0, s.first.size() - 1);
            std::uniform_int_distribution<size_t> coin_flip(0, 1);
            size_t target = pos_dist(rng);

            // TODO Actually we sometimes don't move - this can be better
            // Move up or down a random element of the solution
            for (auto &el : s.first) {
                if (s_tick.size() == s.first.size() - target - 1) {
                    size_t max = luts[get_lut_param(el.first.cell)].size() - 1;
                    if (max == 0) {
                        s_tick[el.first] = 0;
                    } else {
                        size_t decrease = el.second > 0 ? el.second - 1 : el.second + 1;
                        size_t increase = el.second < max ? el.second + 1 : el.second - 1;
                        s_tick[el.first] = coin_flip(rng) == 1 ? increase : decrease;
                    }
                } else {
                    s_tick[el.first] = el.second;
                }
            }

            return {s_tick, evaluator.value(s_tick)};
        }

        void erase_dominated(archive_t<E> &arch) const {
            arch.erase(std::remove_if(arch.begin(), arch.end(), [&](const archive_entry_t<E> &s_tick) {
                for (auto &s : arch) {
                    if (evaluator.dominates(s, s_tick))
                        return true;
                }
                return false;
            }), arch.end());
        }

        static inline double accept_probability(double delta_avg, double temp) {
            return 1.0 / (1.0 + std::exp(delta_avg * temp));
        }
    };
}

// TODO Add a "progress tracker class"
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

#endif //YOSYS_ALS_OPTIMIZER_H
