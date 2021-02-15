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
    std::vector<double> curr_epsmax(processor_count, 0);
    std::vector<std::thread> threads;

    size_t n_vectors = 1u << ctx->g.num_inputs;
    size_t slice = n_vectors / processor_count;

    for (size_t j = 0; j < processor_count; j++) {
        size_t start = j * slice;
        size_t end = std::min(start + slice, n_vectors);

        threads.emplace_back([this, &s, &curr_epsmax, start, end, j]() {
            for (size_t i = start; i < end; i++) {
                auto result = evaluate_graph(s, boost::dynamic_bitset<>(ctx->g.num_inputs, i)).to_ulong();
                if (result > curr_epsmax[j]) {
                    curr_epsmax[j] = result;
                }
            }
        });
    }

    for (auto &t : threads)
        t.join();

    auto max_result = std::max_element(curr_epsmax.begin(), curr_epsmax.end());

    return *max_result;
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
