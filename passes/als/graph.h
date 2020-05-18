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

#ifndef YOSYS_ALS_GRAPH_H
#define YOSYS_ALS_GRAPH_H

#include "kernel/yosys.h"
#include "lut_catalog.h"
#include <boost/dynamic_bitset.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>

namespace yosys_als {

    /// The vertex type for topological analysis of the circuit
    struct vertex_t {
        enum {CONSTANT_ZERO, CONSTANT_ONE, PRIMARY_INPUT, CELL} type;
        Yosys::IdString name;
        Yosys::Cell *cell;

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

	/// The representation of the graph
	typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS, vertex_t, edge_t> adj_list_t;
	
	/// The vertex descriptor type for topological analysis of the circuit
	typedef boost::graph_traits<adj_list_t>::vertex_descriptor vertex_descriptor_t;
	
	/// The edge descriptor type for topological analysis of the circuit
	typedef boost::graph_traits<adj_list_t>::edge_descriptor edge_descriptor_t;

	typedef Yosys::dict<vertex_t, size_t> ax_configuration_t;

	/// The graph type for topological analysis of the circuit
	class graph_t
	{
		private:
				
			/** List of nodes and adjacent nodes */
			adj_list_t adj_list;
			
			/** Vector of graph nodes, in topological order */
			std::vector<vertex_descriptor_t> ordered_vertices;
			
			/** Number of primary input cells */
			size_t primary_inputs;

			void from_module(Yosys::Module * const module);
		
		public:
			
			/** Build an empty graph */
			graph_t() {}
			
			/**
			 * @brief Create a graph with the topological structure of the circuit
			 * @param module A module
			 * @return A graph with the topological structure of the circuit
			 */
	    	explicit graph_t(Yosys::Module * const module)
	    	{
				from_module(module);
				boost::topological_sort(adj_list, std::back_inserter(ordered_vertices));
				std::reverse(ordered_vertices.begin(), ordered_vertices.end());
			}
			
			/// Directly accesses a vertex
			adj_list_t::vertex_bundled& operator[](adj_list_t::vertex_descriptor v) {return adj_list[v];}
			
			/// Directly accesses a vertex
			const adj_list_t::vertex_bundled& operator[](adj_list_t::vertex_descriptor v) const	{return adj_list[v];}
			
			// Direclty accesses an edge
			adj_list_t::edge_bundled& operator[](adj_list_t::edge_descriptor e)	{ return adj_list[e]; }
			
			/// Directly accesses an edge
			const adj_list_t::edge_bundled& operator[](adj_list_t::edge_descriptor e) const{return adj_list[e];}

			const std::vector<vertex_descriptor_t>& get_ordered_vertices() const {return ordered_vertices;}
			
			size_t get_primary_inputs() const {return primary_inputs;}
			
			const adj_list_t& get_adj_list() const {return adj_list;}
	};
	
}

#endif //YOSYS_ALS_GRAPH_H
