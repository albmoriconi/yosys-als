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

/**
 * @file
 * @brief Graph utility functions for Yosys ALS module
 */

#ifndef YOSYS_ALS_GRAPH_H
#define YOSYS_ALS_GRAPH_H

#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "kernel/yosys.h"

#if defined __GNUC__
#pragma GCC diagnostic pop
#endif

#include <boost/graph/adjacency_list.hpp>

namespace yosys_als {

/// The vertex type for topological analysis of the circuit
struct vertex_t {
    enum {
        CONSTANT_ZERO, CONSTANT_ONE, PRIMARY_INPUT, CELL, WEIGHTED_CELL
    } type;
    Yosys::IdString name;
    Yosys::Cell *cell;

    size_t weight;

    unsigned int hash() const {
        return name.hash();
    }

    bool operator==(const vertex_t &rhs) const {
        return name == rhs.name;
    }
};

/// The edge type for topological analysis of the circuit
struct edge_t {
    size_t connection;
    size_t signal;
};

/// The graph type for topological analysis of the circuit
typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS, vertex_t, edge_t> graph_t;

/// A bundle of a graph with additional informations
struct Graph {
    graph_t g;
    size_t num_inputs{0};
};

/// The vertex descriptor type for topological analysis of the circuit
typedef boost::graph_traits<graph_t>::vertex_descriptor vertex_d;

/// The edge descriptor type for topological analysis of the circuit
typedef boost::graph_traits<graph_t>::edge_descriptor edge_d;

/**
 * @brief Create a graph with the topological structure of the circuit
 * @param module A module
 * @return A graph with the topological structure of the circuit
 */
Graph graph_from_module(Yosys::Module *module, const Yosys::dict<Yosys::SigBit, double>& weights);
}

#endif //YOSYS_ALS_GRAPH_H
