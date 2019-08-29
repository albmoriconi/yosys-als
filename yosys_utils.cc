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
 * @brief RTLIL and Yosys utility functions for Yosys ALS module
 */
// TODO Use debug parameters consistently

#include "yosys_utils.h"

USING_YOSYS_NAMESPACE

namespace yosys_als {

    aig_model_t synthesize_lut(const Const &lut, const unsigned int ax_degree, bool debug) {
        if (debug)
            log("LUT %s Ax: %d... ", lut.as_string().c_str(), ax_degree);

        auto aig = yosys_als::synthesize_lut(boost::dynamic_bitset<>(lut.as_string()), ax_degree);

        if (debug)
            log("satisfied with %d gates.\n", aig.num_gates);

        return aig;
    }

    void apply_mapping(Module *const module, const dict<IdString, aig_model_t> &mapping, bool debug) {
        for (auto &sub : mapping) {
            replace_lut(module, sub);

            if (debug)
                log("Replaced %s in %s.\n", sub.first.c_str(), module->name.c_str());
        }
    }

    size_t count_cells(const Module *const module, const IdString &type) {
        size_t cell_count = 0;

        // Don't use count_if (avoid converting ObjRange to vector)
        for (auto &cell : module->cells_) {
            if (type.empty() || cell.second->type == type)
                cell_count++;
        }

        return cell_count;
    }

    Module *cloneInDesign(const Module *const source, const IdString &copy_id, Design *const design) {
        Module *copy = source->clone();

        copy->name = copy_id;
        copy->design = design;
        copy->attributes.erase("\\top");

        if (copy->design)
            copy->design->modules_[copy_id] = copy;

        return copy;
    }

    void clean_and_freduce(Module *const module) {
        Pass::call_on_module(module->design, module, "clean");
        Pass::call_on_module(module->design, module, "freduce");
        Pass::call_on_module(module->design, module, "clean");
    }

    // FIXME Rewrite this (code smells)
    // 1. create our SAT instance
    // 2. handle different problems
    // 3. remove hard-coded strings
    bool checkSat(const Module *const module) {
        std::ifstream oldFile("axmiter.json");
        if (oldFile.good())
            std::remove("axmiter.json");

        Pass::call(module->design, "sat -prove trigger 0 -dump_json axmiter.json axmiter");

        std::ifstream newFile("axmiter.json");
        if (newFile.good()) {
            std::remove("axmiter.json");
            return false;
        }

        return true;
    }

    // TODO Rewrite this (decompose)
    void replace_lut(Module *const module, const pair<IdString, aig_model_t> &lut) {
        // Vector of variables in the model
        std::array<SigSpec, 2> vars;
        vars[1].append(State::S0);

        // Get LUT ins and outs
        SigSpec lut_out;
        for (auto &conn : module->cell(lut.first)->connections()) {
            if (module->cell(lut.first)->input(conn.first))
                vars[1].append(conn.second);
            else if (module->cell(lut.first)->output(conn.first))
                lut_out = conn.second;
        }

        // Create AND gates
        std::array<std::vector<Wire *>, 2> and_ab;
        for (int i = 0; i < lut.second.num_gates; i++) {
            Wire *and_a = module->addWire(NEW_ID);
            Wire *and_b = module->addWire(NEW_ID);
            Wire *and_y = module->addWire(NEW_ID);
            module->addAndGate(NEW_ID, and_a, and_b, and_y);
            and_ab[0].push_back(and_a);
            and_ab[1].push_back(and_b);
            vars[1].append(and_y);
        }

        // Negate variables
        for (auto &sig : vars[1]) {
            Wire *not_y = module->addWire(NEW_ID);
            module->addNotGate(NEW_ID, sig, not_y);
            vars[0].append(not_y);
        }

        // Create connections
        assert(GetSize(and_ab[0]) == GetSize(and_ab[1]));
        assert(GetSize(vars[0]) == GetSize(vars[1]));
        for (int i = 0; i < GetSize(and_ab[0]); i++) {
            for (int c = 0; c < GetSize(and_ab); c++) {
                int g_idx = lut.second.num_inputs + i;
                int p = lut.second.p[g_idx][c];
                int s = lut.second.s[g_idx][c];
                module->connect(and_ab[c][i], vars[p][s]);
            }
        }
        module->connect(lut_out, vars[lut.second.out_p][lut.second.out]);

        // Delete LUT
        module->remove(module->cell(lut.first));
    }
}
