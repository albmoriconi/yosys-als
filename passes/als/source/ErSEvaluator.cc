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

// [[CITE]] An Error-Oriented Test Methodology to Improve Yield with Error-Tolerance
// Tong-Yu Hsieh, Kuen-Jong Lee, and Melvin A. Breuer

/**
 * @file
 * @brief Solution evaluator for Yosys ALS module
 */

#include "ErSEvaluator.h"

#include <map>
#include <numeric>
#include <thread>

namespace yosys_als {

    ErSEvaluator::ErSEvaluator(optimizer_context_t<ErSEvaluator> *ctx) : ctx(ctx) {
        processor_count = std::thread::hardware_concurrency();

        // May return 0 on some systems?
        if (processor_count < 1)
            processor_count = 1;
    }

    void ErSEvaluator::setup(const parameters_t &parameters) {
        // Count reliability normalization factor
        rel_norm = 0.0;
        for (auto &w : ctx->weights)
            rel_norm += w.second;

        // Count gates baseline
        gates_baseline = gates(ctx->opt->empty_solution().first);

        // Set parameters
        test_vectors_n = parameters.test_vectors_n;

        // Create samples and evaluate exact outputs
        if (ctx->g.num_inputs >= 8 * sizeof(unsigned long))
            throw std::range_error("Circuit has too many inputs");

        size_t total_vectors = 1ul << ctx->g.num_inputs;
        test_vectors = selection_sample(test_vectors_n, total_vectors);
        exact_outputs.reserve(test_vectors.size());
        for (auto &v : test_vectors)
            exact_outputs.emplace_back(evaluate_graph(ctx->opt->empty_solution().first, v));
    }

    ErSEvaluator::value_t ErSEvaluator::value(const solution_t &s) const {
        if (test_vectors.size() < 1000)
            return value_t{1 - circuit_reliability(s),
                           static_cast<double>(gates(s)) / gates_baseline};
        else
            return value_t{1 - circuit_reliability_smt(s),
                           static_cast<double>(gates(s)) / gates_baseline};
    }

    ErSEvaluator::value_t ErSEvaluator::empty_solution_value(const solution_t &s) {
        (void) s;
        return {0, 1};
    }

    bool ErSEvaluator::dominates(const archive_entry_t<ErSEvaluator> &s1,
            const archive_entry_t<ErSEvaluator> &s2, double arel_bias) {
        double arel1 = fabs(arel_bias - s1.second[0]);
        double arel2 = fabs(arel_bias - s2.second[0]);
        double gate1 = s1.second[1];
        double gate2 = s2.second[1];

        return (arel1 <= arel2 && gate1 < gate2) || (arel1 < arel2 && gate1 <= gate2);
    }

    /*
     * Private methods
     */

    std::vector<boost::dynamic_bitset<>> ErSEvaluator::selection_sample(const unsigned long n,
            const unsigned long max) {
        std::uniform_real_distribution<double> U(0.0, 1.0);
        std::vector<boost::dynamic_bitset<>> sample;

        if (n >= max) {
            sample.reserve(max);
            for (unsigned long t = 0; t < max; t++) {
                sample.emplace_back(ctx->g.num_inputs, t);
            }
        } else {
            sample.reserve(n);
            for (unsigned long t = 0, m = 0; m < n && t < max; t++) {
                if ((max - t) * U(rng) < (n - m)) {
                    sample.emplace_back(ctx->g.num_inputs, t);
                    m++;
                }
            }
        }

        // Don't worry about this return - it is not copied
        return sample;
    }

    double ErSEvaluator::circuit_reliability(const solution_t &s) const {
        size_t exact = 0;

        for (size_t i = 0; i < test_vectors.size(); i++) {
            if (evaluate_graph(s, test_vectors[i]) == exact_outputs[i]) {
                exact++;
            }
        }

        double r_s = static_cast<double>(exact) / test_vectors.size();
        size_t n_s = test_vectors.size();

        if ((10ul * n_s) < (1ul << ctx->g.num_inputs)) {
            double estimated_rel = r_s + (4.5 / n_s) * (1 + sqrt(1 + (4.0 / 9.0) * n_s * r_s * (1 - r_s)));
            if (estimated_rel > 1.0)
                return r_s;
            else
                return estimated_rel;
        } else {
            return r_s;
        }
    }

    double ErSEvaluator::circuit_reliability_smt(const solution_t &s) const {
        std::vector<size_t> exact(processor_count, 0);
        std::vector<std::thread> threads;
        size_t slice = test_vectors.size() / processor_count;

        for (size_t j = 0; j < processor_count; j++) {
            size_t start = j * slice;
            size_t end = std::min(start + slice, test_vectors.size());

            threads.emplace_back([this, &s, &exact, start, end, j] () {
                for (size_t i = start; i < end; i++) {
                    if (evaluate_graph(s, test_vectors[i]) == exact_outputs[i]) {
                        exact[j]++;
                    }
                }
            });
        }

        for (auto &t : threads)
            t.join();

        size_t exact_tot = std::accumulate(exact.begin(), exact.end(), (size_t)0);
        double r_s = static_cast<double>(exact_tot) / test_vectors.size();
        size_t n_s = test_vectors.size();

        if ((10ul * n_s) < (1ul << ctx->g.num_inputs)) {
            double estimated_rel = r_s + (4.5 / n_s) * (1 + sqrt(1 + (4.0 / 9.0) * n_s * r_s * (1 - r_s)));
            if (estimated_rel > 1.0)
                return r_s;
            else
                return estimated_rel;
        } else {
            return r_s;
        }
    }

    size_t ErSEvaluator::gates(const solution_t &s) const {
        size_t count = 0;

        for (auto &v : s)
            count += ctx->luts[get_lut_param(v.first.cell)][v.second].num_gates;

        return count;
    }

    boost::dynamic_bitset<> ErSEvaluator::evaluate_graph(const solution_t &s,
            const boost::dynamic_bitset<> &input) const {
        // Yosys::dict does not seem to be thread-safe w.r.t. write access?
        // TODO If we had a max of vertex_d we could simply use an array
        std::map<vertex_d, bool> cell_value;
        std::string output;
        size_t curr_input = 0; // ugly, but dynamic_bitset has no iterators

        for (auto &v : ctx->vertices) {
            // Distinguish between PIs/constants and all other nodes (i.e. cells)
            if (boost::in_degree(v, ctx->g.g) == 0) {
                // Assign input value to vertex (no check on cardinality)
                if (ctx->g.g[v].type == vertex_t::PRIMARY_INPUT)
                    cell_value[v] = input[curr_input++];
                else // Constant
                    cell_value[v] = (ctx->g.g[v].type == vertex_t::CONSTANT_ONE);
            } else {
                // Construct the input for the cell
                auto in_edges = boost::in_edges(v, ctx->g.g);
                std::string cell_input;
                std::for_each(in_edges.first, in_edges.second, [&](const edge_d &e) {
                    cell_input += cell_value[boost::source(e, ctx->g.g)] ? "1" : "0";
                });

                // Evaluate cell output value from inputs - we only cover the LUT case
                auto lut_specification =
                        ctx->luts.at(get_lut_param(ctx->g.g[v].cell))[s.at(ctx->g.g[v])].fun_spec;
                size_t lut_entry = std::stoul(cell_input, nullptr, 2);
                cell_value[v] = lut_specification[lut_entry];

                if (boost::out_degree(v, ctx->g.g) == 0) { // Primary outputs
                    // maybe it's faster to append directly to a bitset? we should profile
                    output += cell_value[v] ? "1" : "0";
                }
            }
        }

        return boost::dynamic_bitset<>(output);
    }
}
