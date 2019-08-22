//
// Created by Alberto Moriconi on 2019-08-22.
//

#include "lut_synthesis.h"
#include "z3++.h"
#include <cmath>
#include <string>
#include <vector>

namespace smt {
// TODO Document me
// https://stackoverflow.com/a/49812018
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

// TODO Document me
    inline bool truth_value(const int i, const int t) {
        if (i == 0)
            return false;

        return t % static_cast<int>(pow(2, i)) >= static_cast<int>(pow(2, i - 1));
    }

// TODO Document me
    inline bool truth_value(const char c) {
        return c != '0';
    }

// TODO Document me
    void function_semantics_constraints(z3::solver &slv, const std::string &fun_spec, const int ax_degree,
                                        const std::vector<z3::expr_vector> &b, const z3::expr &out_p) {
        std::string fun_spec_rev(fun_spec.rbegin(), fun_spec.rend());

        if (ax_degree == 0)
            for (size_t t = 0; t < fun_spec.size(); t++)
                slv.add(b.back()[t] == (!out_p != slv.ctx().bool_val(truth_value(fun_spec_rev[t]))));
        else {
            z3::expr_vector ax(slv.ctx());
            for (size_t t = 0; t < fun_spec.size(); t++) {
                ax.push_back(slv.ctx().bool_const(vformat("ax_%d", t).c_str()));
                slv.add(ax[t] == (b.back()[t] != (!out_p != slv.ctx().bool_val(truth_value(fun_spec_rev[t])))));
                slv.add(z3::atmost(ax, ax_degree));
            }
        }
    }

// TODO Document me
// TODO Make this faster
    aig_model lut_synthesis(const std::string &fun_spec, const int ax_degree) {
        auto num_vars = static_cast<size_t>(ceil(log2(fun_spec.size())));
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
        std::vector<std::vector<z3::expr_vector>> a(2);
        std::vector<z3::expr_vector> s;
        s.emplace_back(ctx);
        s.emplace_back(ctx);
        std::vector<z3::expr_vector> p;
        p.emplace_back(ctx);
        p.emplace_back(ctx);

        // Function semantics
        z3::expr out_p = ctx.bool_const("p");
        slv.push();
        function_semantics_constraints(slv, fun_spec, ax_degree, b, out_p);

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
                for (size_t j = 0; j < i; j++)
                    for (size_t c = 0; c < s.size(); c++)
                        slv.add(z3::implies(s[c][i_gates] == static_cast<int>(j),
                                            a[c][i_gates][t] == (b[j][t] != !p[c][i_gates])));
            }

            // Update function semantics
            slv.push();
            function_semantics_constraints(slv, fun_spec, ax_degree, b, out_p);
        }

        z3::model m = slv.get_model();

        aig_model aig;
        aig.num_inputs = num_vars;
        for (size_t i = 0; i < s[0].size(); i++) {
            aig.s.emplace_back(m.eval(s[0][i]).get_numeral_int(), m.eval(s[1][i]).get_numeral_int());
            aig.p.emplace_back(m.eval(p[0][i]).is_true(), m.eval(p[1][i]).is_true());
        }
        aig.out_p = m.eval(out_p).is_true();

        return aig;
    }
}
