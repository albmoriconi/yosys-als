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

#include "graph.h"
#include "kernel/yosys.h"
#include "kernel/sigtools.h"

#include <boost/graph/adjacency_list.hpp>

USING_YOSYS_NAMESPACE

namespace yosys_als {

    Graph graph_from_module(Module *const module) {
        Graph g;
        SigMap sigmap(module);
        dict<IdString, Vertex> vertex_map;
        dict<SigBit, Cell*> driver_of;

        // Iterate on cells, add them as vertices, build driver index
        for (auto cell : module->cells()) {
            auto v = boost::add_vertex(g);
            g[v].name = cell->name;
            g[v].type = vertex_t::CELL;
            vertex_map[cell->name] = v;

            for (auto &conn : cell->connections())
                if (cell->output(conn.first))
                    for (auto &sig : sigmap(conn.second))
                        driver_of[sig] = cell;
        }

        // Add driver -> driven cell edges
        boost::optional<Vertex> zero_v = boost::none;
        boost::optional<Vertex> one_v = boost::none;
        for (auto cell : module->cells()) {
            size_t conn_idx = 0;

            for (auto &conn : cell->connections()) {
                size_t sig_idx = 0;

                if (cell->input(conn.first)) {
                    for (auto &sig : sigmap(conn.second)) {
                        // Check if signal has driver (i.e. not a PI)
                        auto driver = driver_of.find(sig);
                        Edge e;
                        bool b;

                        if (driver != driver_of.end()) {
                            // In this case, add an edge and mark it with the progressive input number
                            boost::tie(e, b) = boost::add_edge(vertex_map[driver_of[sig]->name], vertex_map[cell->name], g);
                        } else {
                            // Otherwise, driver is a PI
                            if (sig.wire != nullptr) {
                                // If it's a wire...
                                auto input = vertex_map.find(sig.wire->name);

                                if (input == vertex_map.end()) {
                                    auto v = boost::add_vertex(g);
                                    g[v].name = sig.wire->name;
                                    g[v].type = vertex_t::PRIMARY_INPUT;
                                    vertex_map[sig.wire->name] = v;
                                }

                                boost::tie(e, b) = boost::add_edge(vertex_map[sig.wire->name], vertex_map[cell->name], g);
                            } else {
                                // If it's a constant...
                                if (sig.data == State::S1) {
                                    if (!one_v) {
                                        auto v = boost::add_vertex(g);
                                        g[v].type = vertex_t::CONSTANT_ONE;
                                        one_v = v;
                                    }

                                    boost::tie(e, b) = boost::add_edge(*one_v, vertex_map[cell->name], g);
                                } else {
                                    if (!zero_v) {
                                        auto v = boost::add_vertex(g);
                                        g[v].type = vertex_t::CONSTANT_ZERO;
                                        zero_v = v;
                                    }

                                    boost::tie(e, b) = boost::add_edge(*zero_v, vertex_map[cell->name], g);
                                }
                            }
                        }

                        g[e].connection = conn_idx;
                        g[e].signal = sig_idx++;
                    }
                }

                conn_idx++;
            }
        }

        // TODO Check if BGL implements move semantics - return a pointer otherwise
        return g;
    }
}
