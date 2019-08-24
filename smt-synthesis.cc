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

#include "smt-synthesis.h"
#include "z3++.h"
#include <array>

namespace smt {

    /*
     * Utility functions and procedures
     */

    /**
     * \brief Memory-safe string formatting for variable names
     * @param zcFormat Null-terminated format string
     * @param ... Variable argument list containing the data to print
     * @return The formatted string
     */
    std::string vformat(const char *const zcFormat, ...) {
        va_list vaArgs;
        va_start(vaArgs, zcFormat);
        va_list vaArgsCopy;
        va_copy(vaArgsCopy, vaArgs);
        const int iLen = std::vsnprintf(nullptr, 0, zcFormat, vaArgsCopy);
        va_end(vaArgsCopy);
        std::vector<char> zc(iLen + 1);
        std::vsnprintf(zc.data(), zc.size(), zcFormat, vaArgs);
        va_end(vaArgs);
        return std::string(zc.data(), iLen);
    }

    /**
     * \brief Check if power of two
     * @param x An unsigned integer
     * @return \c true if \c v is a power of 2, otherwise \c false
     */
    constexpr bool is_power_of_2(unsigned x) {
        return x && ((x & (x - 1)) == 0);
    }

    /**
     * \brief Fast ceil log2
     * @param x An unsigned integer
     * @return The ceil log2 of \c x
     */
    unsigned ceil_log2(unsigned x) {
        assert(x > 0);

#if defined(__GNUC__)
        return x > 1 ? (8 * sizeof(x)) - __builtin_clz(x - 1) : 0;
#else
        if (x > 1) {
            for (size_t i = 0; i < 8 * sizeof(x); i++)
                if (((x - 1) >> i) == 0)
                    return i;
        }

        return 0;
#endif
    }

    /**
     * \brief Truth table value
     * @param i Variable index
     * @param t Row of the table
     * @return The value of given variable (variable 0 is always \c false)
     */
    inline bool truth_value(const unsigned i, const unsigned t) {
        if (i == 0)
            return false;

        return t % (1u << i) >= (1u << (i - 1));
    }


    /**
     * \brief Truth value of character
     * @param c A character
     * @return \c false if \c is \c '0', otherwise \c true
     */
    constexpr bool truth_value(const char c) {
        return c != '0';
    }

    /**
     * \brief Return a column of the truth table
     * @param col The number of the column
     * @param num_vars The number of variables
     * @param pol The polarity of the column
     * @return The column of the truth table (column 0 is all 0s)
     */
    std::string truth_column(const unsigned i, const unsigned num_vars, const bool pol) {
        std::string col;
        size_t length = (1u << num_vars);

        for (size_t t = 0; t < length; t++)
            col.append(1, (truth_value(i, t) == pol) ? '1' : '0');

        return std::string(col.rbegin(), col.rend());
    }

    /**
     * \brief Distance between two string
     * @param s1 A string
     * @param s2 A string
     * @return Number of different characters
     */
    int distance(const std::string &s1, const std::string &s2) {
        assert(s1.size() == s2.size());

        int dist = 0;
        for (size_t i = 0; i < s1.size(); i++) {
            if (s1[i] != s2[i])
                dist++;
        }

        return dist;
    }

    /**
     * \brief Determines if a specification can be satisfied by a single variable
     * @param fun_spec A function specification
     * @param ax_degree The approximation degree
     * @return The variable in AIGER convention (multiplied by 2, plus 1 if negated)
     */
    int is_single_var(const std::string &fun_spec, const int ax_degree) {
        unsigned num_vars = ceil_log2(fun_spec.size());

        for (size_t i = 0; i < num_vars + 1; i++) {
            if (distance(fun_spec, truth_column(i, num_vars, true)) <= ax_degree)
                return static_cast<int>(i) * 2;
            else if (distance(fun_spec, truth_column(i, num_vars, false)) <= ax_degree)
                return (static_cast<int>(i) * 2) + 1;
        }

        return -1;
    }

    /**
     * \brief Enforces function semantics for given specification
     * @param slv The SMT solver
     * @param fun_spec The function specification
     * @param ax_degree The approximation degree
     * @param b The variables that represents the AND gates outputs
     * @param out_p The variable that represents the polarity of the output
     */
    void function_semantics_constraints(z3::solver &slv, const std::string &fun_spec, const int ax_degree,
                                        const std::vector<z3::expr_vector> &b, const z3::expr &out_p) {
        std::string rev_spec(fun_spec.rbegin(), fun_spec.rend());

        if (ax_degree == 0) {
            // Exact semantics
            for (size_t t = 0; t < fun_spec.size(); t++)
                slv.add(b.back()[t] == (!out_p != slv.ctx().bool_val(truth_value(rev_spec[t]))));
        } else {
            // Bit-distance semantics
            z3::expr_vector ax(slv.ctx());
            for (size_t t = 0; t < fun_spec.size(); t++) {
                ax.push_back(slv.ctx().bool_const(vformat("ax_%d", t).c_str()));
                slv.add(ax[t] == (b.back()[t] != (!out_p != slv.ctx().bool_val(truth_value(rev_spec[t])))));
                slv.add(z3::atmost(ax, ax_degree));
            }
        }
    }


    /*
     * Exposed functions and procedures
     */

    // TODO Make this faster
    aig_model_t lut_synthesis(const std::string &fun_spec, const int ax_degree) {
        assert(!fun_spec.empty() && is_power_of_2(fun_spec.size()));
        assert(ax_degree >= 0);

        unsigned num_vars = ceil_log2(fun_spec.size());

        aig_model_t aig;
        aig.num_inputs = static_cast<int>(num_vars) + 1;
        for (size_t i = 0; i < num_vars + 1; i++) {
            aig.s.emplace_back(std::array<int, 2>{static_cast<int>(i), static_cast<int>(i)});
            aig.p.emplace_back(std::array<int, 2>{1, 1});
        }

        int sel_var;
        if ((sel_var = is_single_var(fun_spec, ax_degree)) != -1) {
            aig.num_gates = 0;
            aig.out = sel_var / 2;
            aig.out_p = (sel_var % 2 == 0) ? 1 : 0;
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
                slv.add(b[i][t] == ctx.bool_val(truth_value(i, t)));
            }
        }

        // Input values, AIG structure and polarity
        std::array<std::vector<z3::expr_vector>, 2> a;
        std::array<z3::expr_vector, 2> s{ctx, ctx};
        std::array<z3::expr_vector, 2> p{ctx, ctx};

        // Function semantics
        z3::expr out_p = ctx.bool_const("p");
        slv.push();
        function_semantics_constraints(slv, fun_spec, ax_degree, b, out_p);

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
            function_semantics_constraints(slv, fun_spec, ax_degree, b, out_p);
        }

        // Get the solver model
        z3::model m = slv.get_model();

        // Populate the AIG model
        for (size_t i = 0; i < s[0].size(); i++) {
            aig.s.emplace_back(std::array<int, 2>{m.eval(s[0][i]).get_numeral_int(), m.eval(s[1][i]).get_numeral_int()});
            aig.p.emplace_back(std::array<int, 2>{m.eval(p[0][i]).is_true(), m.eval(p[1][i]).is_true()});
        }
        aig.num_gates = static_cast<int>(aig.s.size()) - aig.num_inputs;
        aig.out = static_cast<int>(aig.s.size()) - 1;
        aig.out_p = m.eval(out_p).is_true();

        return aig;
    }
}
