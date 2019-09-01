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

#include "smtsynth.h"
#include "utils.h"

#include <boolector/boolector.h>
#include <boost/dynamic_bitset.hpp>
#include <boost/optional.hpp>

#include <array>
#include <vector>

namespace yosys_als {

    /*
     * Utility data structures
     */

    /**
     * Context of the current SMT problem
     */
    struct smt_context_t {
        /// The function specification
        boost::dynamic_bitset<> fun_spec;

        /// The maximum hamming distance of the output
        unsigned int out_distance{};

        /// The solver
        Btor *btor{};

        /// The main bitvector sort
        BoolectorSort bitvec_sort{};

        /// The boolean sort
        BoolectorSort bool_sort{};

        /// Boolean false value
        BoolectorNode *bool_false{};

        // Boolean true value
        BoolectorNode *bool_true{};

        /// The truth table entries
        std::vector<std::vector<BoolectorNode *>> b;

        /// The inputs to the Maj gates
        std::array<std::vector<std::vector<BoolectorNode *>>, 3> a;

        /// The structure of the MIG
        std::array<std::vector<BoolectorNode *>, 3> s;

        /// The polarities of the Maj gates inputs
        std::array<std::vector<BoolectorNode *>, 3> p;

        /// The output polarity of the MIG
        BoolectorNode *out_p{};
    };

    /**
     * @brief Initialize a new SMT solver context
     * @return A context with a new solver
     */
    smt_context_t smt_context_new() {
        smt_context_t ctx;

        ctx.btor = boolector_new();
        boolector_set_opt(ctx.btor, BTOR_OPT_MODEL_GEN, 1);
        boolector_set_opt(ctx.btor, BTOR_OPT_AUTO_CLEANUP, 1);

        ctx.bitvec_sort = boolector_bitvec_sort(ctx.btor, 8);
        ctx.bool_sort = boolector_bitvec_sort(ctx.btor, 1);

        ctx.bool_false = boolector_int(ctx.btor, 0, ctx.bool_sort);
        ctx.bool_true = boolector_int(ctx.btor, 1, ctx.bool_sort);

        return ctx;
    }

    /**
     * @brief Releases the resources of the SMT context
     */
    void smt_context_delete(const smt_context_t &ctx) {
        boolector_delete(ctx.btor);
    }

    /**
     * @brief SMT value for Boolean
     * @return The correct value in SMT context for given Boolean
     */
     BoolectorNode *smt_context_bool(const smt_context_t &ctx, const bool val) {
         return val ? ctx.bool_true : ctx.bool_false;
     }

    /*
     * Utility functions and procedures
     */

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
     * @brief Enforces function semantics for given specification
     * @param ctx The SMT solver context
     * @param fun_spec The function specification
     * @param out_distance The maximum hamming distance of the synthesized function
     */
    void enforce_function_semantics(const smt_context_t &ctx) {
        if (ctx.out_distance == 0) {
            // Exact semantics
            for (size_t t = 0; t < ctx.fun_spec.size(); t++) {
                auto not_out_p = boolector_not(ctx.btor, ctx.out_p);
                auto func_value = smt_context_bool(ctx, ctx.fun_spec[t]);
                auto eq_rh = boolector_xor(ctx.btor, not_out_p, func_value);
                boolector_assert(ctx.btor, boolector_eq(ctx.btor, ctx.b.back()[t], eq_rh));
            }
        } else {
            // Hamming distance semantics
            //z3::expr_vector ax(slv.ctx());
            //for (size_t t = 0; t < fun_spec.size(); t++) {
            //    ax.push_back(slv.ctx().bool_const(vformat("ax_%d", t).c_str()));
            //    slv.add(ax[t] == (b.back()[t] != (!out_p != slv.ctx().bool_val(fun_spec[t]))));
            //    slv.add(z3::atmost(ax, out_distance));
            //}
        }
    }

    /*
     * Exposed functions and procedures
     */

    mig_model_t synthesize_lut(const boost::dynamic_bitset<> &fun_spec, const unsigned int out_distance) {
        if (fun_spec.empty() || !is_power_of_2(fun_spec.size()))
            throw std::invalid_argument("Function specification is invalid.");

        auto num_vars = ceil_log2(fun_spec.size());

        // Variables for constant 0 and PIs
        mig_model_t mig;
        mig.num_inputs = num_vars + 1;
        for (size_t i = 0; i < num_vars + 1; i++) {
            mig.s.emplace_back(0, i, i);
            mig.p.emplace_back(false, true, true);
        }

        // Single variable
        if (auto sel_var = single_var(fun_spec, out_distance)) {
            mig.num_gates = 0;
            mig.out = *sel_var / 2;
            mig.out_p = *sel_var % 2 == 0;
            return mig;
        }

        // Initialize solver
        auto ctx = smt_context_new();
        ctx.fun_spec = fun_spec;
        ctx.out_distance = out_distance;

        // Entries of the truth table
        for (size_t i = 0; i < num_vars + 1; i++) {
            for (size_t t = 0; t < ctx.fun_spec.size(); t++) {
                ctx.b[i].push_back(boolector_var(ctx.btor, ctx.bool_sort, nullptr));
                auto truth_table_entry = smt_context_bool(ctx, truth_table_value(i, t));
                boolector_assert(ctx.btor, boolector_eq(ctx.btor, ctx.b[i][t], truth_table_entry));
            }
        }

        // Function semantics
        boolector_push(ctx.btor, 1);
        enforce_function_semantics(ctx);

        // Solver loop
        while (boolector_sat(ctx.btor) == BOOLECTOR_UNSAT) {
            // Update index
            auto i = ctx.b.size();
            auto i_gates = i - (num_vars + 1);

            // Drop old function semantics constraints
            boolector_pop(ctx.btor, 1);

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
            boolector_assert(ctx.btor, boolector_ult(ctx.btor, ctx.s[1][i_gates], ctx.s[2][i_gates]));
            boolector_assert(ctx.btor, boolector_or(ctx.btor, ctx.p[0][i_gates], ctx.p[1][i_gates]));
            boolector_assert(ctx.btor, boolector_or(ctx.btor, ctx.p[0][i_gates], ctx.p[2][i_gates]));
            boolector_assert(ctx.btor, boolector_or(ctx.btor, ctx.p[1][i_gates], ctx.p[2][i_gates]));

            for (size_t t = 0; t < ctx.fun_spec.size(); t++) {
                // Maj functionality
                ctx.b[i].push_back(boolector_var(ctx.btor, ctx.bool_sort, nullptr));
                for (size_t c = 0; c < ctx.a.size(); c++)
                    ctx.a[c][i_gates].push_back(boolector_var(ctx.btor, ctx.bool_sort, nullptr));
                auto maj_prod_1 = boolector_and(ctx.btor, ctx.a[0][i_gates][t], ctx.a[1][i_gates][t]);
                auto maj_prod_2 = boolector_and(ctx.btor, ctx.a[0][i_gates][t], ctx.a[2][i_gates][t]);
                auto maj_prod_3 = boolector_and(ctx.btor, ctx.a[1][i_gates][t], ctx.a[2][i_gates][t]);
                auto maj_sum_1 = boolector_or(ctx.btor, maj_prod_1, maj_prod_2);
                boolector_assert(ctx.btor, boolector_or(ctx.btor, maj_sum_1, maj_prod_3));

                // Input connections
                for (size_t j = 0; j < i; j++) {
                    for (size_t c = 0; c < ctx.s.size(); c++) {
                        auto j_bv = boolector_int(ctx.btor, j, ctx.bitvec_sort);
                        auto impl_lh = boolector_eq(ctx.btor, ctx.s[c][i_gates], j_bv);
                        auto not_p = boolector_not(ctx.btor, ctx.p[c][i_gates]);
                        auto impl_rh_eq_rh = boolector_xor(ctx.btor, ctx.b[j][t], not_p);
                        auto impl_rh = boolector_eq(ctx.btor, ctx.a[c][i_gates][t], impl_rh_eq_rh);
                        boolector_implies(ctx.btor, impl_lh, impl_rh);
                    }
                }
            }

            // Update function semantics
            boolector_push(ctx.btor, 1);
            enforce_function_semantics(ctx);
        }

        // Get the solver model
//        z3::model m = slv.get_model();
//
//        // Populate the AIG model
//        for (size_t i = 0; i < s[0].size(); i++) {
//            mig.s.emplace_back(std::array<int, 2> {m.eval(s[0][i]).get_numeral_int(),
//                                                   m.eval(s[1][i]).get_numeral_int()});
//            mig.p.emplace_back(std::array<int, 2> {m.eval(p[0][i]).is_true(),
//                                                   m.eval(p[1][i]).is_true()});
//        }
//        mig.num_gates = static_cast<int>(mig.s.size()) - mig.num_inputs;
//        mig.out = static_cast<int>(mig.s.size()) - 1;
//        mig.out_p = m.eval(out_p).is_true();

        // Delete solver
        smt_context_delete(ctx);

        return mig;
    }
} // namespace yosys_als