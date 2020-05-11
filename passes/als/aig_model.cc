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

// [[CITE]] Exact Synthesis of Majority-Inverter Graphs and Its Applications
// Mathias Soeken, Luca Gaetano Amarù, Pierre-Emmanuel Gaillardon, and Giovanni De Micheli (2017)

/**
 * @file
 * @brief SMT synthesis for Yosys ALS module
 */

#include "aig_model.h"
#include "smt_utils.h"
#include "smt_context.h"
#include <array>
#include <vector>

namespace yosys_als {

/**
 * @brief Tries to satisfy a specification with a single variable
 * @param fun_spec A function specification
 * @param out_distance The maximum hamming distance of the synthesized function
 * @return The index of the variable in AIGER convention, or \c boost::none
 */
boost::optional<size_t> single_var(const boost::dynamic_bitset<> &fun_spec, const unsigned int out_distance) {
    auto num_vars = ceil_log2(fun_spec.size());

    for (size_t i = 0; i < num_vars + 1; i++) {
        if (hamming_distance(fun_spec, truth_table_column(i, num_vars, true)) <= out_distance)
            return i * 2;
        else if (hamming_distance(fun_spec, truth_table_column(i, num_vars, false)) <= out_distance)
            return (i * 2) + 1;
    }

    return boost::none;
}

/**
 * @brief SMT AIG exact synthesis for given function specification
 * 
 * @param fun_spec The function specification 
 * 
 * @param out_distance The maximum allowed hamming distance between the
 * given function specification and the actually synthesized function
 * 
 * @return The synthesized AIG model
 */
aig_model_t synthesize_lut(const boost::dynamic_bitset<> &fun_spec, const unsigned int out_distance) {
    if (fun_spec.empty() || !is_power_of_2(fun_spec.size()))
        throw std::invalid_argument("Function specification is invalid.");

	// Getting the number of inputs
    auto num_vars = ceil_log2(fun_spec.size());

    // Variables for constant 0 and PIs
    aig_model_t aig;
    aig.num_inputs = num_vars + 1;
    for (size_t i = 0; i < num_vars + 1; i++) {
        aig.s.emplace_back(std::array<size_t, 2>{i, i});
        aig.p.emplace_back(std::array<bool, 2>{true, true});
    }

    // Check that the problem is trivial, i.e. it can be solved with a constant
	// or a single node.
    if (auto sel_var = single_var(fun_spec, out_distance)) {
		// In this case, it is useless to instantiate the SMT solver.
        aig.num_gates = 0;
        aig.out = *sel_var / 2;
        aig.out_p = *sel_var % 2 == 0;
        aig.fun_spec = truth_table_column(aig.out, num_vars, aig.out_p);
        return aig;
    }

    // Initialize the solver, setting the function specification and the
	// maximum Hamming distance allowed
    auto ctx = smt_context_new();
    ctx.fun_spec = fun_spec;
    ctx.out_distance = out_distance;

    // Adding constraints for input variables
	// QUESTION perché è necessario codificare sia qui che all'interno del ciclo i constraint per le variabili di ingresso?
    for (size_t i = 0; i < num_vars + 1; i++) {
        ctx.b.emplace_back();
        for (size_t t = 0; t < ctx.fun_spec.size(); t++) {
            ctx.b[i].push_back(boolector_var(ctx.btor, ctx.bool_sort, nullptr));
            auto truth_table_entry = smt_context_bool(ctx, truth_table_value(i, t));
            boolector_assert(ctx.btor, boolector_eq(ctx.btor, ctx.b[i][t], truth_table_entry));
        }
    }

    // Adding constraints for the semantics of the Boolean function
    assume_function_semantics(ctx);

    // Solver loop
	// QUESTION In pratica che si fa in questo ciclo?
    while (boolector_sat(ctx.btor) == BOOLECTOR_UNSAT) {
        // Update index
        auto i = ctx.b.size();
        auto i_gates = i - (num_vars + 1);

        // Add lists for t entries for gate i
        ctx.b.emplace_back();
        for (auto &vec : ctx.a)
            vec.emplace_back();

        // Adding constraints constraints for:
		// - no cycles
		// - order between operand indexes
        for (size_t c = 0; c < ctx.s.size(); c++) {
            ctx.s[c].push_back(boolector_var(ctx.btor, ctx.bitvec_sort, nullptr));
            auto ult_rh = boolector_int(ctx.btor, i, ctx.bitvec_sort);
            boolector_assert(ctx.btor, boolector_ult(ctx.btor, ctx.s[c][i_gates], ult_rh));
            auto ugte_zero = boolector_zero(ctx.btor, ctx.bitvec_sort);
			boolector_assert(ctx.btor, boolector_ugte(ctx.btor, ctx.s[c][i_gates], ugte_zero));
            ctx.p[c].push_back(boolector_var(ctx.btor, ctx.bool_sort, nullptr));
        }
		
        boolector_assert(ctx.btor, boolector_ult(ctx.btor, ctx.s[0][i_gates], ctx.s[1][i_gates]));

        for (size_t t = 0; t < ctx.fun_spec.size(); t++) {
            // Maj functionality
            ctx.b[i].push_back(boolector_var(ctx.btor, ctx.bool_sort, nullptr));
            for (size_t c = 0; c < ctx.a.size(); c++)
                ctx.a[c][i_gates].push_back(boolector_var(ctx.btor, ctx.bool_sort, nullptr));

            auto and_f = boolector_and(ctx.btor, ctx.a[0][i_gates][t], ctx.a[1][i_gates][t]);
            boolector_assert(ctx.btor, boolector_eq(ctx.btor, ctx.b[i][t], and_f));

            // Adding constraints for input variables
			// QUESTION perché è necessario codificare sia qui che all'interno del ciclo i constraint per le variabili di ingresso?
			// QUESTION Serve per la propagazione?
            for (size_t j = 0; j < i; j++) {
                for (size_t c = 0; c < ctx.s.size(); c++) {
                    auto j_bv = boolector_int(ctx.btor, j, ctx.bitvec_sort);
                    auto impl_lh = boolector_eq(ctx.btor, ctx.s[c][i_gates], j_bv);
                    auto not_p = boolector_not(ctx.btor, ctx.p[c][i_gates]);
                    auto impl_rh_eq_rh = boolector_xor(ctx.btor, ctx.b[j][t], not_p);
                    auto impl_rh = boolector_eq(ctx.btor, ctx.a[c][i_gates][t], impl_rh_eq_rh);
                    boolector_assert(ctx.btor, boolector_implies(ctx.btor, impl_lh, impl_rh));
                }
            }
        }

        // Updating function semantics
        assume_function_semantics(ctx);
    }

    // Populate the AIG model
    boost::dynamic_bitset<> inc_fun_spec;
    for (size_t i = 0; i < ctx.b.back().size(); i++)
        inc_fun_spec.push_back(smt_context_assignment_bool(
                ctx, ctx.b.back()[i]) ^ !smt_context_assignment_bool(ctx, ctx.out_p));
    aig.fun_spec = inc_fun_spec;

    for (size_t i = 0; i < ctx.s[0].size(); i++) {
        aig.s.emplace_back(std::array<size_t, 2>{smt_context_assignment_uint(ctx, ctx.s[0][i]),
                                                 smt_context_assignment_uint(ctx, ctx.s[1][i])});
        aig.p.emplace_back(std::array<bool, 2>{smt_context_assignment_bool(ctx, ctx.p[0][i]),
                                               smt_context_assignment_bool(ctx, ctx.p[1][i])});
    }
    aig.num_gates = aig.s.size() - aig.num_inputs;
    aig.out = aig.s.size() - 1;
    aig.out_p = smt_context_assignment_bool(ctx, ctx.out_p);

    // Delete solver
    smt_context_delete(ctx);

    return aig;
}
	/**
	 * @brief Write an aig_model_t object to a binary file, using an ofstream object
	 * @param os stream object
	 * @param aig aig_model_t instance
	 * @return the stream object
	 */
	std::ofstream & operator<<(std::ofstream &os, const aig_model_t &aig)
	{
		size_t fun_spec_size = aig.fun_spec.size();
		unsigned long fun_spec = aig.fun_spec.to_ulong();
		os.write(reinterpret_cast<const char*>(&fun_spec_size), sizeof(size_t));
		os.write(reinterpret_cast<const char*>(&fun_spec), sizeof(unsigned long));
		
		os.write(reinterpret_cast<const char*>(&aig.num_inputs), sizeof(size_t));
		os.write(reinterpret_cast<const char*>(&aig.num_gates), sizeof(size_t));
		
		size_t s_size = aig.s.size();
		os.write(reinterpret_cast<const char*>(&s_size), sizeof(size_t));
		for (auto it : aig.s)
			os.write(reinterpret_cast<const char *>(it.data()), AIG_NODE_CHILDREN * sizeof(size_t));
	
		size_t p_size = aig.p.size();
		os.write(reinterpret_cast<const char*>(&p_size), sizeof(size_t));
		for (auto it : aig.p)
			os.write(reinterpret_cast<const char*>(it.data()), AIG_NODE_CHILDREN * sizeof(bool));
		
		os.write(reinterpret_cast<const char*>(&aig.out), sizeof(size_t));
		os.write(reinterpret_cast<const char*>(&aig.out_p), sizeof(bool));
		return os;
	}
	
	/**
	 * @brief Read an aig_model_t from a binary file, using an ifstream object
	 * @param is stream object
	 * @param aig aig_model_t instance
	 * @return the stream object
	 */
	std::ifstream &operator>>(std::ifstream &is, aig_model_t &aig)
	{
		size_t fun_spec_size;
		unsigned long fun_spec;
		is.read(reinterpret_cast<char*>(&fun_spec_size), sizeof(size_t));
		is.read(reinterpret_cast<char*>(&fun_spec), sizeof(unsigned long));
		aig.fun_spec = boost::dynamic_bitset<>(fun_spec_size, fun_spec);
		
		is.read(reinterpret_cast<char*>(&aig.num_inputs), sizeof(size_t));
		is.read(reinterpret_cast<char*>(&aig.num_gates), sizeof(size_t));
		
		size_t s_size;
		is.read(reinterpret_cast<char*>(&s_size), sizeof(size_t));
		for (size_t i = 0; i < s_size; i++)
		{
			std::array<size_t, AIG_NODE_CHILDREN> tmp;
			is.read(reinterpret_cast<char *>(tmp.data()), AIG_NODE_CHILDREN * sizeof(size_t));
			aig.s.push_back(tmp);
		}
		
		size_t p_size;
		is.read(reinterpret_cast<char*>(&p_size), sizeof(size_t));
		for (size_t i = 0; i < p_size; i++)
		{
			std::array<bool, AIG_NODE_CHILDREN> tmp;
			is.read(reinterpret_cast<char*>(tmp.data()), AIG_NODE_CHILDREN * sizeof(bool));
			aig.p.push_back(tmp);
		}
		
		is.read(reinterpret_cast<char*>(&aig.out), sizeof(size_t));
		is.read(reinterpret_cast<char*>(&aig.out_p), sizeof(bool));
		
		return is;
	}
};

