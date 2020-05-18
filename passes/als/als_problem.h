#ifndef ALS_MOP_PROBLEM_4_AMOSA_H
#define ALS_MOP_PROBLEM_4_AMOSA_H

#include <exception>
#include <vector>
#include <string>
#include <memory>
#include "graph.h"
#include "aig_model.h"
#include "evaluator.h"
#include "yosys_utils.h"
#include "lut_catalog.h"
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

		als_problem_t(graph_t &, lut_catalog_t &, evaluator_t&, bool leave_uninitialized = false);
		
		als_problem_t(const als_problem_t& als_inst) :
			internal_representation(als_inst.internal_representation),
			luts_graph(als_inst.luts_graph),
			luts_catalog(als_inst.luts_catalog),
			evaluator(als_inst.evaluator),
			ff_values(als_inst.ff_values) 
			{}
		
		const als_problem_t& operator=(const als_problem_t &als_inst)
		{
			if (this != &als_inst)
			{
				internal_representation = als_inst.internal_representation;
				luts_graph = als_inst.luts_graph;
				luts_catalog = als_inst.luts_catalog;
				evaluator = als_inst.evaluator;
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
		
		ax_configuration_t get_ax_configuration() const {return internal_representation;}
		
	private:
	
		/** Internal representation of the solution */
		ax_configuration_t internal_representation;

		/** Graph of connected LUTS */
		graph_t &luts_graph;

		/** This is the catalog of synthesized luts. It is shared among all the als_problem_t instances */
		lut_catalog_t &luts_catalog;

		/** Evaluator */
		evaluator_t &evaluator;

		/** Fitness function values for the considered solution */
		std::vector<double> ff_values;
		
		void compute_fitness();
		double compute_number_of_gates() const;
	};

}

#endif
