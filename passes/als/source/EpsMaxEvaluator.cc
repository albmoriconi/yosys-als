/* -*- c++ -*-
 *  yosys-als -- Approximate logic synthesis
 *
 *  Copyright (C) 2021  Alberto Moriconi <albmoriconi@gmail.com>
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

/**
 * @file
 * @brief Maximum absolute error evaluator for Yosys ALS module
 */

#include "EpsMaxEvaluator.h"

#include <thread>

namespace yosys_als {

EpsMaxEvaluator::EpsMaxEvaluator(optimizer_context_t<EpsMaxEvaluator> *ctx) : ctx(ctx) {
    processor_count = std::thread::hardware_concurrency();

    // May return 0 on some systems?
    if (processor_count < 1)
        processor_count = 1;
}

void EpsMaxEvaluator::setup(const parameters_t &parameters) {
    (void) parameters;

    // Count gates baseline
    gates_baseline = gates(ctx->opt->empty_solution().first);

    if (ctx->g.num_inputs > sizeof(unsigned long) * 8 - 1) {
        throw std::runtime_error("Too many inputs - Circuit unsupported");
    }

    std::stable_sort(ctx->vertices.begin(), ctx->vertices.end(),
                     [this](const vertex_d &v1, const vertex_d &v2)
    {
        if (ctx->g.g[v1].weight.has_value() && ctx->g.g[v2].weight.has_value())
            return ctx->g.g[v1].weight.get() > ctx->g.g[v2].weight.get();
        else
            return ctx->g.g[v2].weight.has_value();
    });

    for (size_t i = 0; i < 1ul << ctx->g.num_inputs; i++)
        exact_outputs.emplace_back(evaluate_graph(ctx->opt->empty_solution().first,
                                                  boost::dynamic_bitset<>(ctx->g.num_inputs, i)));
}

EpsMaxEvaluator::value_t EpsMaxEvaluator::value(const solution_t &s) const {
    return value_t{circuit_epsmax(s),
                   static_cast<double>(gates(s)) / gates_baseline};
}

EpsMaxEvaluator::value_t EpsMaxEvaluator::empty_solution_value(const solution_t &s) {
    (void) s;
    return {0, 1};
}

bool EpsMaxEvaluator::dominates(const archive_entry_t<EpsMaxEvaluator> &s1,
                             const archive_entry_t<EpsMaxEvaluator> &s2, double arel_bias) {
    double arel1 = fabs(arel_bias - s1.second[0]);
    double arel2 = fabs(arel_bias - s2.second[0]);
    double gate1 = s1.second[1];
    double gate2 = s2.second[1];

    return (arel1 <= arel2 && gate1 < gate2) || (arel1 < arel2 && gate1 <= gate2);
}

/*
 * Private methods
 */

double EpsMaxEvaluator::circuit_epsmax(const solution_t &s) const {
    double curr_epsmax = 0;

    size_t n_vectors = 1ul << ctx->g.num_inputs;

    for (size_t i = 0; i < n_vectors; i++) {
        auto result = abs(
                static_cast<double>(evaluate_graph(s, boost::dynamic_bitset<>(ctx->g.num_inputs, i)).to_ulong())
                - static_cast<double>(exact_outputs[i].to_ulong())
                );
        if (result > curr_epsmax) {
            curr_epsmax = result;
        }
    }

    return curr_epsmax;
}

size_t EpsMaxEvaluator::gates(const solution_t &s) const {
    size_t count = 0;

    for (auto &v : s)
        count += ctx->luts[get_lut_param(v.first.cell)][v.second].num_gates;

    return count;
}

boost::dynamic_bitset<> EpsMaxEvaluator::evaluate_graph(const solution_t &s,
                                                     const boost::dynamic_bitset<> &input) const {
    // Yosys::dict does not seem to be thread-safe w.r.t. write access?
    // TODO If we had a max of vertex_d we could simply use an array
    std::map<vertex_d, bool> cell_value;
    // We only consider WEIGHTED outputs!
    std::string output(ctx->weights.size(), '0');
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
            std::reverse(cell_input.begin(), cell_input.end());

            // Evaluate cell output value from inputs - we only cover the LUT case
            auto lut_specification =
                    ctx->luts.at(get_lut_param(ctx->g.g[v].cell))[s.at(ctx->g.g[v])].fun_spec;
            size_t lut_entry = std::stoul(cell_input, nullptr, 2);
            cell_value[v] = lut_specification[lut_entry];

            if (boost::out_degree(v, ctx->g.g) == 0) { // Primary outputs
                if (ctx->g.g[v].weight.has_value()) {
                    output[ctx->g.g[v].weight.get()] = cell_value[v] ? '1' : '0';
                }
            }
        }
    }
    std::reverse(output.begin(), output.end());

    return boost::dynamic_bitset<>(output);
}
}
