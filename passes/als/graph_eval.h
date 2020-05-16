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

#ifndef YOSYS_ALS_GRAPH_EVAL_H
#define YOSYS_ALS_GRAPH_EVAL_H

#include "graph.h"

#include <boost/dynamic_bitset.hpp>

namespace yosys_als {

    /**
     * Evaluates the output of a combinatorial circuit in graph form
     * @param graph The graph of the circuit
     * @param topological_order A topological ordering for the circuit nodes
     * @param input The input vector
     * @return The output of the circuit for given input vector
     */
    boost::dynamic_bitset<> evaluate_graph(const graph_t &graph,
                                           const std::vector<vertex_d> &topological_order,
                                           const boost::dynamic_bitset<> &input);
}

#endif //YOSYS_ALS_GRAPH_EVAL_H
