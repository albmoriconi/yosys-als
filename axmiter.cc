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

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct AxMiterWorker {
    bool debug = false;

    void run(Design *design) {
        // TODO Select modules via args
        IdString golden_name = RTLIL::escape_id("golden");
        IdString approximate_name = RTLIL::escape_id("approximate");
        IdString axmiter_name = RTLIL::escape_id("axmiter");

        if (design->modules_.count(golden_name) == 0)
            log_cmd_error("Can't find gold module %s!\n", golden_name.c_str());
        if (design->modules_.count(approximate_name) == 0)
            log_cmd_error("Can't find gate module %s!\n", approximate_name.c_str());
        if (design->modules_.count(axmiter_name) != 0)
            log_cmd_error("There is already a module %s!\n", axmiter_name.c_str());

        Module *golden_module = design->modules_.at(golden_name);
        Module *approximate_module = design->modules_.at(approximate_name);

        // Check matching ports
        for (auto &it : golden_module->wires_) {
            Wire *w1 = it.second, *w2;
            if (w1->port_id == 0)
                continue;
            if (approximate_module->wires_.count(it.second->name) == 0)
                goto match_golden_port_error;
            w2 = approximate_module->wires_.at(it.second->name);
            if (w1->port_input != w2->port_input)
                goto match_golden_port_error;
            if (w1->port_output != w2->port_output)
                goto match_golden_port_error;
            if (w1->width != w2->width)
                goto match_golden_port_error;
            continue;
            match_golden_port_error:
            log_cmd_error("No matching port in approximate module was found for %s!\n", it.second->name.c_str());
        }

        for (auto &it : approximate_module->wires_) {
            Wire *w1 = it.second, *w2;
            if (w1->port_id == 0)
                continue;
            if (golden_module->wires_.count(it.second->name) == 0)
                goto match_approximate_port_error;
            w2 = golden_module->wires_.at(it.second->name);
            if (w1->port_input != w2->port_input)
                goto match_approximate_port_error;
            if (w1->port_output != w2->port_output)
                goto match_approximate_port_error;
            if (w1->width != w2->width)
                goto match_approximate_port_error;
            continue;
            match_approximate_port_error:
            log_cmd_error("No matching port in golden module was found for %s!\n", it.second->name.c_str());
        }

        // Create miter
        auto *axmiter_module = new Module;
        axmiter_module->name = axmiter_name;
        design->add(axmiter_module);

        Cell *golden_cell = axmiter_module->addCell("\\golden", golden_name);
        Cell *approximate_cell = axmiter_module->addCell("\\approximate", approximate_name);

        SigSpec all_conditions;

        // Connect wires in miter
        for (auto &it : golden_module->wires_) {
            Wire *w1 = it.second;

            if (w1->port_input) {
                Wire *w2 = axmiter_module->addWire("\\in_" + unescape_id(w1->name), w1->width);
                w2->port_input = true;

                golden_cell->setPort(w1->name, w2);
                approximate_cell->setPort(w1->name, w2);
            }

            if (w1->port_output) {
                Wire *w2_golden = axmiter_module->addWire("\\golden_" + unescape_id(w1->name), w1->width);
                Wire *w2_approximate = axmiter_module->addWire("\\approximate_" + RTLIL::unescape_id(w1->name), w1->width);

                golden_cell->setPort(w1->name, w2_golden);
                approximate_cell->setPort(w1->name, w2_approximate);

                SigSpec this_condition;

                Cell *sub_cell = axmiter_module->addCell(NEW_ID, "$sub");
                sub_cell->parameters["\\A_WIDTH"] = w2_golden->width;
                sub_cell->parameters["\\B_WIDTH"] = w2_approximate->width;
                sub_cell->parameters["\\Y_WIDTH"] = w2_golden->width + 1;
                // TODO Actually we have to check this
                sub_cell->parameters["\\A_SIGNED"] = 0;
                sub_cell->parameters["\\B_SIGNED"] = 0;
                sub_cell->setPort("\\A", w2_golden);
                sub_cell->setPort("\\B", w2_approximate);
                sub_cell->setPort("\\Y", axmiter_module->addWire("\\subout", w2_golden->width + 1));
                this_condition = sub_cell->getPort("\\Y");
                this_condition.as_wire()->port_output = true;

                all_conditions.append(this_condition);
            }
        }

//        if (all_conditions.size() != 1) {
//            RTLIL::Cell *reduce_cell = miter_module->addCell(NEW_ID, "$reduce_and");
//            reduce_cell->parameters["\\A_WIDTH"] = all_conditions.size();
//            reduce_cell->parameters["\\Y_WIDTH"] = 1;
//            reduce_cell->parameters["\\A_SIGNED"] = 0;
//            reduce_cell->setPort("\\A", all_conditions);
//            reduce_cell->setPort("\\Y", miter_module->addWire(NEW_ID));
//            all_conditions = reduce_cell->getPort("\\Y");
//        }
//
//        if (flag_make_assert) {
//            RTLIL::Cell *assert_cell = miter_module->addCell(NEW_ID, "$assert");
//            assert_cell->setPort("\\A", all_conditions);
//            assert_cell->setPort("\\EN", State::S1);
//        }
//
//        RTLIL::Wire *w_trigger = miter_module->addWire("\\trigger");
//        w_trigger->port_output = true;
//
//        RTLIL::Cell *not_cell = miter_module->addCell(NEW_ID, "$not");
//        not_cell->parameters["\\A_WIDTH"] = all_conditions.size();
//        not_cell->parameters["\\A_WIDTH"] = all_conditions.size();
//        not_cell->parameters["\\Y_WIDTH"] = w_trigger->width;
//        not_cell->parameters["\\A_SIGNED"] = 0;
//        not_cell->setPort("\\A", all_conditions);
//        not_cell->setPort("\\Y", w_trigger);

        axmiter_module->fixup_ports();

        return;
    }
};

struct AxMiterPass : public Pass {
    AxMiterPass() : Pass("axmiter", "approximation miter for worst-error case analysis") {}

    void help() YS_OVERRIDE {
        //   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
        log("\n");
        log("    axmiter [options]\n");
        log("\n");
        log("This command generates an approximation miter for worst-error case analysis.\n");
        log("\n");
        log("    -d\n");
        log("        enable debug output\n");
        log("\n");
    }

    void execute(std::vector<std::string> args, Design *design) YS_OVERRIDE {
        AxMiterWorker worker;

        log_header(design, "Executing AXMITER pass (approximation miter).\n");
        log_push();

        size_t argidx;
        for (argidx = 1; argidx < args.size(); argidx++) {
            if (args[argidx] == "-d") {
                worker.debug = true;
                continue;
            }
        }
        extra_args(args, argidx, design);

        worker.run(design);

        log_pop();
    }
} AxMiterPass;

PRIVATE_NAMESPACE_END
