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

/**
 * @file
 * @brief Graph utility functions for Yosys ALS module
 */

#ifndef YOSYS_ALS_GRAPH_UTILS_H
#define YOSYS_ALS_GRAPH_UTILS_H

#include "kernel/yosys.h"

#include <boost/graph/adjacency_list.hpp>

namespace yosys_als {

    /// The vertex properties for topological analysis of the circuit
    struct vertex_p {
        /// The \c IdString of the cell
        Yosys::IdString name;
    };

    /// The graph type for topological analysis of the circuit
    typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS, vertex_p> Graph;

    /// The vertex descriptor type for topological analysis of the circuit
    typedef boost::graph_traits<Graph>::vertex_descriptor vertex_t;

    /// The edge descriptor type for topological analysis of the circuit
    typedef boost::graph_traits<Graph>::edge_descriptor edge_t;

    Graph graph_from_module(Yosys::Module *module);
}

#endif //YOSYS_ALS_GRAPH_UTILS_H
