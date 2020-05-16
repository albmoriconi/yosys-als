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

/**
 * @file
 * @brief Graph evaluation functions for Yosys ALS module
 */

#include "graph_eval.h"
#include "yosys_utils.h"

#include "kernel/yosys.h"

#include <string>

namespace yosys_als {

    // We bring our topological order to:
    // 1. avoid evaluating it every time
    // 2. have finer control over ensuring it's always the same
    // However if this function is moved somewhere else (e.g. in a class) we can avoid this
    boost::dynamic_bitset<> evaluate_graph(const graph_t &graph,
                                           const std::vector<vertex_d> &topological_order,
                                           const lut_catalogue_t &catalogue,
                                           const Yosys::dict<vertex_t, size_t> &solution,
                                           const boost::dynamic_bitset<> &input) {
        Yosys::dict<vertex_t, bool> cell_value;
        std::string output;
        size_t curr_input = 0; // ugly, but dynamic_bitset has no iterators

        for (auto &v : topological_order) {
            // Distinguish between primary inputs and all other nodes (i.e. cells)
            if (boost::in_degree(v, graph) == 0) {
                // Assign input value to vertex (no check on cardinality)
                cell_value[graph[v]] = input[curr_input++];
            } else {
                // Construct the input for the cell
                auto in_edges = boost::in_edges(v, graph);
                std::string cell_input;
                std::for_each(in_edges.first, in_edges.second, [&](const edge_d &e) {
                    cell_input += cell_value[graph[boost::source(e, graph)]] ? "1" : "0";
                });

                // Evaluate cell output value from inputs - we only cover the LUT case
                auto lut_specification = catalogue.at(get_lut_param(graph[v].cell))[solution.at(graph[v])].fun_spec;
                size_t lut_entry = std::stoul(cell_input, nullptr, 2);
                cell_value[graph[v]] = lut_specification[lut_entry];

                if (boost::out_degree(v, graph) == 0) { // Primary outputs
                    // maybe it's faster to append directly to a bitset? we should profile
                    output += cell_value[graph[v]] ? "1" : "0";
                }
            }
        }

        return boost::dynamic_bitset<>(output);
    }
}
