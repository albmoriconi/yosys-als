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

#include "graph_utils.h"
#include "kernel/yosys.h"
#include "kernel/sigtools.h"

#include <boost/graph/adjacency_list.hpp>

USING_YOSYS_NAMESPACE

namespace yosys_als {

    Graph graph_from_module(Module *const module) {
        Graph g;
        dict<IdString, Vertex> vertex_map;

        // Iterate on cells, add them as vertices
        for (auto cell : module->cells()) {
            auto v = boost::add_vertex(g);
            g[v] = cell->name;
            vertex_map[cell->name] = v;
        }

        // Build a driver index
        SigMap sigmap(module);
        dict<SigBit, Cell*> driver_of;

        for (auto cell : module->cells()) {
            for (auto &conn : cell->connections())
                if (cell->output(conn.first))
                    for (auto &sig : sigmap(conn.second))
                        driver_of[sig] = cell;
        }

        // Add driver -> driven cell edges
        for (auto cell : module->cells()) {
            for (auto &conn : cell->connections())
                if (cell->input(conn.first))
                    for (auto &sig : sigmap(conn.second))
                        // Skip if signal has no driver (i.e. PI)
                        if (driver_of.find(sig) != driver_of.end())
                            boost::add_edge(vertex_map[driver_of[sig]->name], vertex_map[cell->name], g);
        }

        // TODO Check if BGL implements move semantics - return a pointer otherwise
        return g;
    }
}
