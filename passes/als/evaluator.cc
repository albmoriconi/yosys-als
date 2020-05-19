#include "evaluator.h"
#include <random>
#include <boost/dynamic_bitset.hpp>
#include <cmath>

void yosys_als::evaluator_t::set_baseline_solution(const ax_configuration_t &baseline_solution) {
	exact_io_map.erase(exact_io_map.begin(), exact_io_map.end());
	std::random_device rd;
	std::default_random_engine re(rd());
	std::uniform_real_distribution<double> uniform(0.0, 1.0);
	
	size_t max = std::pow(2, luts_graph.get_primary_inputs()) - 1;
	
	size_t n = luts_graph.get_primary_inputs();
	for (unsigned vector = 0; vector < ntests; vector++)
		for (size_t t = 0, m = 0; m < n; t++)
			if ((max - t) * uniform(re) < (n - m))
			{
				boost::dynamic_bitset<> input_vector(n, t);
				boost::dynamic_bitset<> output_vector = evaluate_graph(input_vector, baseline_solution);
				exact_io_map.emplace(exact_io_map.end(), std::make_pair(input_vector, output_vector));
				m++;
			}
}

void yosys_als::evaluator_t::get_error_frequency(
	const yosys_als::ax_configuration_t & ax_configuration,
	double &lower_bound,
	double &upper_bound) const
{
	size_t error_count = 0;
	for (auto &it : exact_io_map)
		if (it.second != evaluate_graph(it.first, ax_configuration))
			error_count++;
	
	double rs = (double) error_count / (double) ntests;
	double center = rs + 4.5 / ntests;
	double range = 4.5 * std::sqrt(1 + 4 * ntests * rs * (1 - rs) / 9 ) / ntests;
	lower_bound = center - range;
	upper_bound = center + range;
}

boost::dynamic_bitset<>
yosys_als::evaluator_t::evaluate_graph(
	const boost::dynamic_bitset<> &input_vector,
	const yosys_als::ax_configuration_t &configuration) const
{
	Yosys::dict<vertex_t, bool> cell_value;
	std::string output;
	size_t curr_input = 0; // ugly, but dynamic_bitset has no iterators
	
	for (auto &v : luts_graph.get_ordered_vertices()) {
		// Distinguish between PIs/constants and all other nodes (i.e. cells)
		if (boost::in_degree(v, luts_graph.get_adj_list()) == 0) {
			// Assign input value to vertex (no check on cardinality)
			if (luts_graph[v].type == vertex_t::PRIMARY_INPUT)
				cell_value[luts_graph[v]] = input_vector[curr_input++];
			else // Constant
				cell_value[luts_graph[v]] = (luts_graph[v].type == vertex_t::CONSTANT_ONE);
		} else {
			// Construct the input for the cell
			auto in_edges = boost::in_edges(v, luts_graph.get_adj_list());
			std::string cell_input;
			std::for_each(in_edges.first, in_edges.second, [&](const edge_descriptor_t &e) {
				cell_input += cell_value[luts_graph[boost::source(e, luts_graph.get_adj_list())]] ? "1" : "0";
			});
			
			// Evaluate cell output value from inputs - we only cover the LUT case
			auto lut_specification = luts_catalog.at(get_lut_param(luts_graph[v].cell))[configuration.at(luts_graph[v])].fun_spec;
			size_t lut_entry = std::stoul(cell_input, nullptr, 2);
			cell_value[luts_graph[v]] = lut_specification[lut_entry];
			
			if (boost::out_degree(v, luts_graph.get_adj_list()) == 0) { // Primary outputs
				// maybe it's faster to append directly to a bitset? we should profile
				output += cell_value[luts_graph[v]] ? "1" : "0";
			}
		}
	}
	return boost::dynamic_bitset<>(output);
}
