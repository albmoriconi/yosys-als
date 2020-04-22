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

#include <iostream>
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

    // Single variable
    if (auto sel_var = single_var(fun_spec, out_distance)) {
        aig.num_gates = 0;
        aig.out = *sel_var / 2;
        aig.out_p = *sel_var % 2 == 0;
        aig.fun_spec = truth_table_column(aig.out, num_vars, aig.out_p);
        return aig;
    }

    // Initialize solver
    auto ctx = smt_context_new();
    ctx.fun_spec = fun_spec;
    ctx.out_distance = out_distance;

    // Entries of the truth table
    for (size_t i = 0; i < num_vars + 1; i++) {
        ctx.b.emplace_back();
        for (size_t t = 0; t < ctx.fun_spec.size(); t++) {
            ctx.b[i].push_back(boolector_var(ctx.btor, ctx.bool_sort, nullptr));
            auto truth_table_entry = smt_context_bool(ctx, truth_table_value(i, t));
            boolector_assert(ctx.btor, boolector_eq(ctx.btor, ctx.b[i][t], truth_table_entry));
        }
    }

    // Function semantics
    assume_function_semantics(ctx);

    // Solver loop
    while (boolector_sat(ctx.btor) == BOOLECTOR_UNSAT) {
        // Update index
        auto i = ctx.b.size();
        auto i_gates = i - (num_vars + 1);

        // Add lists for t entries for gate i
        ctx.b.emplace_back();
        for (auto &vec : ctx.a)
            vec.emplace_back();

        // Structure (no cycles, order, polarity)
        for (size_t c = 0; c < ctx.s.size(); c++) {
            ctx.s[c].push_back(boolector_var(ctx.btor, ctx.bitvec_sort, nullptr));
            auto ult_rh = boolector_int(ctx.btor, i, ctx.bitvec_sort);
            boolector_assert(ctx.btor, boolector_ult(ctx.btor, ctx.s[c][i_gates], ult_rh));
            auto ugte_zero = boolector_zero(ctx.btor, ctx.bitvec_sort);
            boolector_assert(ctx.btor, boolector_ugte(ctx.btor, ctx.s[c][i_gates], ugte_zero));
            ctx.p[c].push_back(boolector_var(ctx.btor, ctx.bool_sort, nullptr));
        }
        boolector_assert(ctx.btor, boolector_ult(ctx.btor, ctx.s[0][i_gates], ctx.s[1][i_gates]));
        //boolector_assert(ctx.btor, boolector_ult(ctx.btor, ctx.s[1][i_gates], ctx.s[2][i_gates]));
        //boolector_assert(ctx.btor, boolector_or(ctx.btor, ctx.p[0][i_gates], ctx.p[1][i_gates]));
        //boolector_assert(ctx.btor, boolector_or(ctx.btor, ctx.p[0][i_gates], ctx.p[2][i_gates]));
        //boolector_assert(ctx.btor, boolector_or(ctx.btor, ctx.p[1][i_gates], ctx.p[2][i_gates]));

        for (size_t t = 0; t < ctx.fun_spec.size(); t++) {
            // Maj functionality
            ctx.b[i].push_back(boolector_var(ctx.btor, ctx.bool_sort, nullptr));
            for (size_t c = 0; c < ctx.a.size(); c++)
                ctx.a[c][i_gates].push_back(boolector_var(ctx.btor, ctx.bool_sort, nullptr));
            //auto maj_prod_1 = boolector_and(ctx.btor, ctx.a[0][i_gates][t], ctx.a[1][i_gates][t]);
            //auto maj_prod_2 = boolector_and(ctx.btor, ctx.a[0][i_gates][t], ctx.a[2][i_gates][t]);
            //auto maj_prod_3 = boolector_and(ctx.btor, ctx.a[1][i_gates][t], ctx.a[2][i_gates][t]);
            //auto maj_sum_1 = boolector_or(ctx.btor, maj_prod_1, maj_prod_2);
            //auto maj = boolector_or(ctx.btor, maj_sum_1, maj_prod_3);
            auto and_f = boolector_and(ctx.btor, ctx.a[0][i_gates][t], ctx.a[1][i_gates][t]);
            boolector_assert(ctx.btor, boolector_eq(ctx.btor, ctx.b[i][t], and_f));

            // Input connections
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

        // Update function semantics
        assume_function_semantics(ctx);
    }

    // Populate the MIG model
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

};

