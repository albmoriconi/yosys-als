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

// TODO Move to utils
/**
 * @file
 * @brief SMT synthesis for Yosys ALS module
 */

#include "smtsynth.h"
#include "utils.h"

#include <boost/dynamic_bitset.hpp>
#include <boost/optional.hpp>
#include <z3++.h>

#include <array>
#include <vector>

namespace yosys_als {

    /*
     * Utility functions and procedures
     */

    /**
     * @brief Tries to satisfy a specification with a single variable
     * @param fun_spec A function specification
     * @param ax_degree The approximation degree
     * @return The index of the variable in AIGER convention, or \c boost::none
     */
    boost::optional<size_t> single_var(const boost::dynamic_bitset<> &fun_spec, const unsigned int ax_degree) {
        unsigned num_vars = ceil_log2(fun_spec.size());

        for (size_t i = 0; i < num_vars + 1; i++) {
            if (hamming_distance(fun_spec, truth_table_column(i, num_vars, true)) <= ax_degree)
                return i * 2;
            else if (hamming_distance(fun_spec, truth_table_column(i, num_vars, false)) <= ax_degree)
                return (i * 2) + 1;
        }

        return boost::none;
    }

    /**
     * @brief Enforces function semantics for given specification
     * @param slv The SMT solver
     * @param fun_spec The function specification
     * @param ax_degree The approximation degree
     * @param b The variables that represents the AND gates outputs
     * @param out_p The variable that represents the polarity of the output
     */
    void enforce_function_semantics(z3::solver &slv, const boost::dynamic_bitset<> &fun_spec,
            const unsigned int ax_degree, const std::vector<z3::expr_vector> &b, const z3::expr &out_p) {
        if (ax_degree == 0) {
            // Exact semantics
            for (size_t t = 0; t < fun_spec.size(); t++)
                slv.add(b.back()[t] == (!out_p != slv.ctx().bool_val(fun_spec[t])));
        } else {
            // Hamming distance semantics
            z3::expr_vector ax(slv.ctx());
            for (size_t t = 0; t < fun_spec.size(); t++) {
                ax.push_back(slv.ctx().bool_const(vformat("ax_%d", t).c_str()));
                slv.add(ax[t] == (b.back()[t] != (!out_p != slv.ctx().bool_val(fun_spec[t]))));
                slv.add(z3::atmost(ax, ax_degree));
            }
        }
    }

    /*
     * Exposed functions and procedures
     */

    // TODO Make this faster and refactor (consider MIG)
    aig_model_t synthesize_lut(const boost::dynamic_bitset<> &fun_spec, const unsigned int ax_degree) {
        if (fun_spec.empty() || !is_power_of_2(fun_spec.size()))
            throw std::invalid_argument("Function specification is invalid.");

        size_t num_vars = ceil_log2(fun_spec.size());

        aig_model_t aig;
        aig.num_inputs = static_cast<int>(num_vars) + 1;
        for (size_t i = 0; i < num_vars + 1; i++) {
            aig.s.emplace_back(std::array<int, 2> {static_cast<int>(i), static_cast<int>(i)});
            aig.p.emplace_back(std::array<int, 2> {1, 1});
        }

        if (auto sel_var = single_var(fun_spec, ax_degree)) {
            aig.num_gates = 0;
            aig.out = static_cast<int>(*sel_var) / 2;
            aig.out_p = (static_cast<int>(*sel_var) % 2 == 0) ? 1 : 0;
            return aig;
        }

        z3::context ctx;
        z3::solver slv(ctx);

        // Entries of the truth table
        std::vector<z3::expr_vector> b;
        for (size_t i = 0; i < num_vars + 1; i++) {
            b.emplace_back(ctx);
            for (size_t t = 0; t < fun_spec.size(); t++) {
                b[i].push_back(ctx.bool_const(vformat("b_%d_%d", i, t).c_str()));
                slv.add(b[i][t] == ctx.bool_val(truth_table_value(i, t)));
            }
        }

        // Input values, AIG structure and polarity
        std::array<std::vector<z3::expr_vector>, 2> a;
        std::array<z3::expr_vector, 2> s{ctx, ctx};
        std::array<z3::expr_vector, 2> p{ctx, ctx};

        // Function semantics
        z3::expr out_p = ctx.bool_const("p");
        slv.push();
        enforce_function_semantics(slv, fun_spec, ax_degree, b, out_p);

        // Solver loop
        // TODO Consider a timeout
        while (!slv.check()) {
            // Update index
            size_t i = b.size();
            size_t i_gates = i - (num_vars + 1);

            // Drop old function semantics constraints
            slv.pop();

            // Add lists for t entries for gate i
            b.emplace_back(z3::expr_vector(ctx));
            for (auto &vec : a)
                vec.emplace_back(ctx);

            // Structure (no cycles, order, polarity)
            for (size_t c = 0; c < s.size(); c++) {
                s[c].push_back(ctx.int_const(vformat("s_%d_%d", c, i + 1).c_str()));
                slv.add(s[c][i_gates] < static_cast<int>(i));
                slv.add(s[c][i_gates] >= 0);
                p[c].push_back(ctx.bool_const(vformat("p_%d_%d", c, i + 1).c_str()));
            }
            slv.add(s[0][i_gates] < s[1][i_gates]);

            for (size_t t = 0; t < fun_spec.size(); t++) {
                // AND functionality
                b[i].push_back(ctx.bool_const(vformat("b_%d_%d", i + 1, t).c_str()));
                for (size_t c = 0; c < s.size(); c++)
                    a[c][i_gates].push_back(ctx.bool_const(vformat("a_%d_%d_%d", c, i + 1, t).c_str()));
                slv.add(b[i][t] == (a[0][i_gates][t] && a[1][i_gates][t]));

                // Input connections
                for (size_t j = 0; j < i; j++) {
                    for (size_t c = 0; c < s.size(); c++)
                        slv.add(z3::implies(s[c][i_gates] == static_cast<int>(j),
                                            a[c][i_gates][t] == (b[j][t] != !p[c][i_gates])));
                }
            }

            // Update function semantics
            slv.push();
            enforce_function_semantics(slv, fun_spec, ax_degree, b, out_p);
        }

        // Get the solver model
        z3::model m = slv.get_model();

        // Populate the AIG model
        for (size_t i = 0; i < s[0].size(); i++) {
            aig.s.emplace_back(std::array<int, 2> {m.eval(s[0][i]).get_numeral_int(),
                                                   m.eval(s[1][i]).get_numeral_int()});
            aig.p.emplace_back(std::array<int, 2> {m.eval(p[0][i]).is_true(),
                                                   m.eval(p[1][i]).is_true()});
        }
        aig.num_gates = static_cast<int>(aig.s.size()) - aig.num_inputs;
        aig.out = static_cast<int>(aig.s.size()) - 1;
        aig.out_p = m.eval(out_p).is_true();

        return aig;
    }
} // namespace yosys_als
