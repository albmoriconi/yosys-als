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

#include "kernel/yosys.h"
#include "kernel/sigtools.h"
#include "z3++.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

// TODO Find a place for this
// TODO Document me
// https://stackoverflow.com/a/49812018
std::string vformat(const char* const zcFormat, ...) {
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
void function_semantics_constraints(z3::solver &slv, const string &fun_spec, const int ax_degree,
                                    const std::vector<z3::expr_vector> &b, const z3::expr &out_p) {
    if (ax_degree == 0)
        for (size_t t = 0; t < fun_spec.size(); t++)
            slv.add(b.back()[t] == (!out_p != slv.ctx().bool_val(truth_value(fun_spec[t]))));
    else {
        z3::expr_vector ax(slv.ctx());
        for (size_t t = 0; t < fun_spec.size(); t++) {
            ax.push_back(slv.ctx().bool_const(vformat("ax_%d", t).c_str()));
            slv.add(ax[t] == (b.back()[t] != (!out_p != slv.ctx().bool_val(truth_value(fun_spec[t])))));
            slv.add(z3::atmost(ax, ax_degree));
        }
    }
}

// TODO Document me
void smt_lut_synthesis(const std::string &fun_spec, const int ax_degree) {
    auto num_vars = static_cast<size_t>(ceil(log2(fun_spec.size())));
    z3::context ctx;
    z3::solver slv(ctx);

    // Entries of the truth table
    std::vector<z3::expr_vector> b;
    for (size_t i = 0; i < num_vars + 1; i++) {
        b.emplace_back(z3::expr_vector(ctx));
        for (size_t t = 0; t < fun_spec.size(); t++) {
            b[i].push_back(ctx.bool_const(vformat("x_%d_%d", i, t).c_str()));
            slv.add(b[i][t] == ctx.bool_val(truth_value(i, t)));
        }
    }

    // Input values, AIG structure and polarity
    std::vector<std::vector<z3::expr_vector>> a(2);
    std::vector<z3::expr_vector> s;
    s.emplace_back(z3::expr_vector(ctx));
    s.emplace_back(z3::expr_vector(ctx));
    std::vector<z3::expr_vector> p;
    p.emplace_back(z3::expr_vector(ctx));
    p.emplace_back(z3::expr_vector(ctx));

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
            vec.emplace_back(z3::expr_vector(ctx));

        // Structure (no cycles, order, polarity)
        for (size_t c = 0; c < s.size(); c++) {
            s[c].push_back(ctx.int_const(vformat("s_%d_%d", c, i).c_str()));
            slv.add(s[c][i_gates] < static_cast<int>(i));
            slv.add(s[c][i_gates] >= 0);
            p[c].push_back(ctx.bool_const(vformat("p_%d_%d", c, i).c_str()));
        }
        slv.add(s[0][i_gates] < s[1][i_gates]);

        for (size_t t = 0; t < fun_spec.size(); t++) {
            // AND functionality
            b[i].push_back(ctx.bool_const(vformat("b_%d_%d", i, t).c_str()));
            for (size_t c = 0; c < s.size(); c++)
                a[c][i_gates].push_back(ctx.bool_const(vformat("a_%d_%d_%d", c, i, t).c_str()));
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

    std::cout << "\n" << slv.get_model() << "\n";
}

struct Als : public Pass {
    Als() : Pass("als", "approximate logic synthesis") {}

    void help() YS_OVERRIDE {
        //   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
        log("\n");
        log("    als [options] [top-level]\n");
        log("\n");
        log("This command executes an approximate logic synthesis.\n");
        log("\n");
    }

    void execute(std::vector<std::string> args, RTLIL::Design *design) YS_OVERRIDE {
        log_header(design, "Executing ALS pass (approximate logic synthesis).\n");

        size_t argidx;
        for (argidx = 1; argidx < args.size(); argidx++) { }
        extra_args(args, argidx, design);

        // 1. Select module, save the old one
        Module *top_mod = nullptr;

        if (design->full_selection()) {
            top_mod = design->top_module();

            if (!top_mod)
                log_cmd_error("Design has no top module, use the 'hierarchy' command to specify one.\n");
        } else {
            auto mods = design->selected_whole_modules();
            if (GetSize(mods) != 1)
                log_cmd_error("Only one top module must be selected.\n");
            top_mod = mods.front();
        }

        auto *design_copy = new RTLIL::Design;
        for (auto &module : design->modules_)
            design_copy->add(module.second->clone());

        // 2. 4-LUT synthesis
        log_push();
        ScriptPass::call(design, "synth -lut 4");
        log("\n");

        // 3. Create signal -> driver dictionary
        update_driver_index(top_mod);

        // 4. Create LUT index
        for (auto cell : top_mod->cells())
            if (cell->hasParam("\\LUT"))
                if (debug)
                    log("LUT: %s (%s)\n",
                            cell->getParam("\\LUT").as_string().c_str(), cell->name.c_str());

        // 5. SMT LUT synthesis
        smt_lut_synthesis("0111100010001000", 0);

        log_pop();
    }

    void update_driver_index(RTLIL::Module *mod) {
        SigMap sigmap(mod);
        sigbit_to_driver_index.clear();

        for (auto cell : mod->cells())
            for (auto &conn : cell->connections())
                if (cell->output(conn.first))
                    for (auto &bit : sigmap(conn.second)) {
                        sigbit_to_driver_index[bit] = cell;
                        if (debug)
                            log("Indexed %s -> %s\n", bit.wire->name.c_str(), cell->name.c_str());
                    }
    }

    dict<SigBit, Cell*> sigbit_to_driver_index;
    bool debug = true;
} AlsPass;

PRIVATE_NAMESPACE_END
