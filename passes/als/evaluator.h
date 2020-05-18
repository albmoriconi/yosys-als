#ifndef EVALUATOR_HSIEH_CLASS_H
#define EVALUATOR_HSIEH_CLASS_H

#include <unordered_map>
#include <string>
#include "graph.h"
#include "lut_catalog.h"

namespace yosys_als
{

class evaluator_t
{
	public:
		evaluator_t(graph_t & graph, lut_catalog_t & catalog, size_t ntest_vectors = 10000) :
			luts_graph(graph),
			luts_catalog(catalog),
			ntests(ntest_vectors)
			{}

		virtual ~evaluator_t() {}

		evaluator_t(const evaluator_t& ev) :
			luts_graph(ev.luts_graph),
			luts_catalog(ev.luts_catalog),
			ntests(ev.ntests),
			exact_io_map(ev.exact_io_map)
			{}
			
		const evaluator_t& operator=(const evaluator_t &ev)
		{
			if (this != &ev)
			{
				luts_graph = ev.luts_graph;
				luts_catalog = ev.luts_catalog;
				ntests = ev.ntests;
				exact_io_map = ev.exact_io_map;
			}
			return *this;
		}
		
		void set_baseline_solution(const ax_configuration_t&);
		
		void get_error_frequency(const ax_configuration_t&, double &lower_bound, double &upper_bound) const;

	private:
		graph_t &luts_graph;
		lut_catalog_t &luts_catalog;
		size_t ntests;
		std::unordered_map<std::string, std::string> exact_io_map;

		std::string evaluate_graph(const std::string&, const ax_configuration_t&) const;
		
};

}

#endif
