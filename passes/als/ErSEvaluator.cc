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

namespace yosys_als {

    ErSEvaluator::ErSEvaluator(optimizer_context_t<ErSEvaluator> *ctx) : ctx(ctx) {
        // Count reliability normalization factor
        rel_norm = 0.0;
        for (auto &w : ctx->weights)
            rel_norm += w.second;

        // Count gates baseline
        gates_baseline = gates(ctx->opt->empty_solution().first);

        // Create samples and evaluate exact outputs
        size_t total_vectors = 1u << ctx->g.num_inputs;
        test_vectors = selection_sample(test_vectors_n, total_vectors);
        exact_outputs.reserve(test_vectors.size());
        for (auto &v : test_vectors)
            exact_outputs.emplace_back(evaluate_graph(ctx->opt->empty_solution().first, v));
    }

    ErSEvaluator::value_t ErSEvaluator::value(const solution_t &s) const {
        return value_t{1 - circuit_reliability(s),
                       static_cast<double>(gates(s)) / gates_baseline};
    }

    bool ErSEvaluator::dominates(const archive_entry_t<ErSEvaluator> &s1,
            const archive_entry_t<ErSEvaluator> &s2, double arel_bias) const {
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

        if ((10 * n_s) < (1u << ctx->g.num_inputs))
            return r_s + (4.5 / n_s) * (1 + sqrt(1 + (4.0 / 9.0) * n_s * r_s * (1 - r_s)));
        else
            return r_s;

    }

    size_t ErSEvaluator::gates(const solution_t &s) const {
        size_t count = 0;

        for (auto &v : s)
            count += ctx->luts[get_lut_param(v.first.cell)][v.second].num_gates;

        return count;
    }

    boost::dynamic_bitset<> ErSEvaluator::evaluate_graph(const solution_t &s,
            const boost::dynamic_bitset<> &input) const {
        Yosys::dict<vertex_t, bool> cell_value;
        std::string output;
        size_t curr_input = 0; // ugly, but dynamic_bitset has no iterators

        for (auto &v : ctx->vertices) {
            // Distinguish between PIs/constants and all other nodes (i.e. cells)
            if (boost::in_degree(v, ctx->g.g) == 0) {
                // Assign input value to vertex (no check on cardinality)
                if (ctx->g.g[v].type == vertex_t::PRIMARY_INPUT)
                    cell_value[ctx->g.g[v]] = input[curr_input++];
                else // Constant
                    cell_value[ctx->g.g[v]] = (ctx->g.g[v].type == vertex_t::CONSTANT_ONE);
            } else {
                // Construct the input for the cell
                auto in_edges = boost::in_edges(v, ctx->g.g);
                std::string cell_input;
                std::for_each(in_edges.first, in_edges.second, [&](const edge_d &e) {
                    cell_input += cell_value[ctx->g.g[boost::source(e, ctx->g.g)]] ? "1" : "0";
                });

                // Evaluate cell output value from inputs - we only cover the LUT case
                auto lut_specification =
                        ctx->luts.at(get_lut_param(ctx->g.g[v].cell))[s.at(ctx->g.g[v])].fun_spec;
                size_t lut_entry = std::stoul(cell_input, nullptr, 2);
                cell_value[ctx->g.g[v]] = lut_specification[lut_entry];

                if (boost::out_degree(v, ctx->g.g) == 0) { // Primary outputs
                    // maybe it's faster to append directly to a bitset? we should profile
                    output += cell_value[ctx->g.g[v]] ? "1" : "0";
                }
            }
        }

        return boost::dynamic_bitset<>(output);
    }
}
