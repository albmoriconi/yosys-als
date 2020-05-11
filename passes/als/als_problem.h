#ifndef ALS_MOP_PROBLEM_4_AMOSA_H
#define ALS_MOP_PROBLEM_4_AMOSA_H

#include <exception>
#include <vector>
#include <string>
#include <memory>
#include "graph.h"
#include "aig_model.h"
#include "yosys_utils.h"
#include "kernel/yosys.h"
#include "kernel/sigtools.h"

namespace yosys_als 
{

/**
 * @brief Approximate Logic Synthesis (ALS) problem formulation for the AMOSA optimization algotithm.
 * 
 * @details
 */
	class als_problem_t
	{
	public:

		als_problem_t(graph_t &, std::vector<vertex_d> &, lut_catalogue_t &, bool leave_uninitialized = false);
		
		als_problem_t(const als_problem_t& als_inst) :
			internal_representation(als_inst.internal_representation),
			luts_graph(als_inst.luts_graph),
			vertices(als_inst.vertices),
			luts_catalog(als_inst.luts_catalog),
			ff_values(als_inst.ff_values) 
			{}
		
		const als_problem_t& operator=(const als_problem_t &als_inst)
		{
			if (this != &als_inst)
			{
				internal_representation = als_inst.internal_representation;
				luts_graph = als_inst.luts_graph;
				vertices = als_inst.vertices;
				luts_catalog = als_inst.luts_catalog;
				ff_values = als_inst.ff_values;
			}
			return *this;
		}
		
		/// For ALS-Problem, randomize() is not available
		void randomize() {throw std::runtime_error("als_mop_problem_t::randomize() is not available");}
		
		als_problem_t neighbor() const;

		/// For ALS-Problem, perturbate() calls neighbor()
		als_problem_t perturbate() const {return neighbor();}

		double distance(const als_problem_t & a_solution) const;

		/**
		 * @brief Get fitness function values for a given point of the solution space
		 *
		 * @param values std::vector where valuer will be placed.
		 */
		void get_fitness_values(std::vector<double>& values) const
		{
			values.erase(values.begin(), values.end());
			values.insert(values.begin(), ff_values.cbegin(), ff_values.cend());
		}

		std::string to_string() const;
		
		/**
		 * Internal representation of a solution:
		 *  - first: the circuit representation, using the AIG structure;
		 *  - second: the approximation degree
		 */
		typedef Yosys::dict<vertex_t, size_t> solution_t;

		solution_t get_solution() const {return internal_representation;}
		
	private:
	
		/** Internal representation of the solution */
		Yosys::dict<vertex_t, size_t> internal_representation;

		/** Graph of connected LUTS */
		graph_t &luts_graph;

		/** Array of vertices of luts_graph, in tolopolical order */
		std::vector<vertex_d> &vertices;

		/** This is the catalog of synthesized luts. It is shared among all the als_problem_t instances */
		lut_catalogue_t &luts_catalog;

		/** Fitness function values for the considered solution */
		std::vector<double> ff_values;
		
		void compute_fitness();

		double compute_number_of_gates() const;
	};

}

#endif
