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

// TODO Move to passes/als directory
/**
 * @file
 * @brief Approximation miter pass for Yosys ALS module
 */

#include "kernel/yosys.h"

#include <boost/dynamic_bitset.hpp>

#include <algorithm>

USING_YOSYS_NAMESPACE

namespace yosys_als {

    // TODO Document members
    struct AxMiterWorker {
        bool debug = false;
        int threshold = 0;

        IdString golden_name;
        IdString approximate_name;
        IdString axmiter_name;

        // TODO Document method
        // TODO Refactor method
        void run(Design *const design) {
            if (design->modules_.count(golden_name) == 0)
                log_cmd_error("Can't find golden module %s!\n", golden_name.c_str());
            if (design->modules_.count(approximate_name) == 0)
                log_cmd_error("Can't find approximate module %s!\n", approximate_name.c_str());
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

            SigSpec all_differences;

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
                    Wire *w2_approximate = axmiter_module->addWire("\\approximate_" + RTLIL::unescape_id(w1->name),
                                                                   w1->width);

                    golden_cell->setPort(w1->name, w2_golden);
                    approximate_cell->setPort(w1->name, w2_approximate);

                    SigSpec this_difference;

                    Cell *sub_cell = axmiter_module->addCell(NEW_ID, "$sub");
                    sub_cell->parameters["\\A_WIDTH"] = w2_golden->width;
                    sub_cell->parameters["\\B_WIDTH"] = w2_approximate->width;
                    sub_cell->parameters["\\Y_WIDTH"] = w2_golden->width;
                    // TODO Actually we have to check this
                    sub_cell->parameters["\\A_SIGNED"] = 0;
                    sub_cell->parameters["\\B_SIGNED"] = 0;
                    sub_cell->setPort("\\A", w2_golden);
                    sub_cell->setPort("\\B", w2_approximate);
                    sub_cell->setPort("\\Y", axmiter_module->addWire(NEW_ID, w2_golden->width));
                    this_difference = sub_cell->getPort("\\Y");

                    all_differences.append(this_difference);
                }
            }

            // TODO Find something more meaningful to do with the differences
            // Idea: specify via args how to bulk the output, then ensure there is a single diff
            if (GetSize(all_differences) != 1)
                all_differences = all_differences.chunks()[0];

            Wire *w_trigger = axmiter_module->addWire("\\trigger");
            w_trigger->port_output = true;

            // Low-weight comparator
            std::string threshold_s;
            to_string(boost::dynamic_bitset<>(all_differences.as_wire()->width, threshold), threshold_s);
            std::reverse(threshold_s.begin(), threshold_s.end());
            int last_one_pos = threshold_s.find_last_of('1');

            SigSpec all_comparisons;

            // See Ceska et al.
            // TODO Comment this ASAP, before i forget it
            for (int i = 0; i < last_one_pos; i++) {
                if (threshold_s[i] == '0') {
                    SigSpec this_comparison;
                    this_comparison.append(all_differences.bits()[i]);

                    for (int j = i + 1; j <= last_one_pos; j++) {
                        if (threshold_s[j] == '1')
                            this_comparison.append(all_differences.bits()[j]);
                    }

                    if (this_comparison.size() > 1) {
                        Cell *reduce_cell = axmiter_module->addCell(NEW_ID, "$reduce_and");
                        reduce_cell->parameters["\\A_WIDTH"] = this_comparison.size();
                        reduce_cell->parameters["\\Y_WIDTH"] = 1;
                        reduce_cell->parameters["\\A_SIGNED"] = 0;
                        reduce_cell->setPort("\\A", this_comparison);
                        reduce_cell->setPort("\\Y", axmiter_module->addWire(NEW_ID));
                        all_comparisons.append(reduce_cell->getPort("\\Y"));
                    }
                }
            }

            log("%s\n", log_signal(all_comparisons));

            for (int i = last_one_pos + 1; i < GetSize(threshold_s) - 1; i++)
                all_comparisons.append(all_differences.bits()[i]);

            log("%s\n", log_signal(all_comparisons));

            std::string threshold_s_n;
            to_string(boost::dynamic_bitset<>(all_differences.as_wire()->width, threshold - 1), threshold_s_n);
            std::reverse(threshold_s_n.begin(), threshold_s_n.end());
            int last_one_pos_n = threshold_s_n.find_last_of('1');

            SigSpec all_comparisons_n;

            for (int i = 0; i < last_one_pos; i++) {
                if (threshold_s_n[i] == '0') {
                    Wire *diff_zero_out = axmiter_module->addWire(NEW_ID);
                    axmiter_module->addNotGate(NEW_ID, all_differences.bits()[i], diff_zero_out);

                    SigSpec this_comparison;
                    this_comparison.append(diff_zero_out);

                    for (int j = i + 1; j <= last_one_pos; j++) {
                        if (threshold_s_n[j] == '1') {
                            Wire *diff_one_out = axmiter_module->addWire(NEW_ID);
                            axmiter_module->addNotGate(NEW_ID, all_differences.bits()[j], diff_one_out);
                            this_comparison.append(diff_one_out);
                        }
                    }

                    if (this_comparison.size() > 1) {
                        Cell *reduce_cell = axmiter_module->addCell(NEW_ID, "$reduce_and");
                        reduce_cell->parameters["\\A_WIDTH"] = this_comparison.size();
                        reduce_cell->parameters["\\Y_WIDTH"] = 1;
                        reduce_cell->parameters["\\A_SIGNED"] = 0;
                        reduce_cell->setPort("\\A", this_comparison);
                        reduce_cell->setPort("\\Y", axmiter_module->addWire(NEW_ID));
                        all_comparisons_n.append(reduce_cell->getPort("\\Y"));
                    }
                }
            }

            for (int i = last_one_pos_n + 1; i < GetSize(threshold_s_n) - 1; i++) {
                Wire *diff_last_out = axmiter_module->addWire(NEW_ID);
                axmiter_module->addNotGate(NEW_ID, all_differences.bits()[i], diff_last_out);
                all_comparisons_n.append(diff_last_out);
            }

            log("%s\n", log_signal(all_comparisons));
            Wire *w_pos = axmiter_module->addWire(NEW_ID);
            Cell *or_pos_cell = axmiter_module->addCell(NEW_ID, "$reduce_or");
            or_pos_cell->parameters["\\A_WIDTH"] = all_comparisons.size();
            or_pos_cell->parameters["\\Y_WIDTH"] = 1;
            or_pos_cell->parameters["\\A_SIGNED"] = 0;
            or_pos_cell->setPort("\\A", all_comparisons);
            or_pos_cell->setPort("\\Y", w_pos);

            Wire *w_neg = axmiter_module->addWire(NEW_ID);
            Cell *or_neg_cell = axmiter_module->addCell(NEW_ID, "$reduce_or");
            or_neg_cell->parameters["\\A_WIDTH"] = all_comparisons_n.size();
            or_neg_cell->parameters["\\Y_WIDTH"] = w_trigger->width;
            or_neg_cell->parameters["\\A_SIGNED"] = 0;
            or_neg_cell->setPort("\\A", all_comparisons_n);
            or_neg_cell->setPort("\\Y", w_neg);

            Wire *not_sign = axmiter_module->addWire(NEW_ID);
            axmiter_module->addNotGate(NEW_ID, all_differences[GetSize(all_differences) - 1], not_sign);
            Wire *pos_trigger = axmiter_module->addWire(NEW_ID);
            axmiter_module->addAndGate(NEW_ID, w_pos, not_sign, pos_trigger);

            Wire *neg_trigger = axmiter_module->addWire(NEW_ID);
            axmiter_module->addAndGate(NEW_ID, w_neg, all_differences[GetSize(all_differences) - 1], neg_trigger);

            axmiter_module->addOrGate(NEW_ID, pos_trigger, neg_trigger, w_trigger);

            axmiter_module->fixup_ports();
        }
    };

    struct AxMiterPass : public Pass {
        AxMiterPass() : Pass("axmiter", "approximation miter for worst-error case analysis") {}

        void help() YS_OVERRIDE {
            //   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
            log("\n");
            log("    axmiter [options] golden_name approximate_name axmiter_name\n");
            log("\n");
            log("This command generates an approximation miter for worst-error case analysis.\n");
            log("\n");
            log("    -d\n");
            log("        enable debug output\n");
            log("\n");
            log("    -threshold <N>\n");
            log("        specify threshold for approximation miter\n");
            log("\n");
        }

        // TODO Fix arguments
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
                if (args[argidx] == "-threshold" && argidx + 1 < args.size()) {
                    worker.threshold = atoi(args[++argidx].c_str());
                    continue;
                }
                break;
            }

            if (argidx + 3 != args.size() || args[argidx].compare(0, 1, "-") == 0)
                cmd_error(args, argidx, "command argument error");

            worker.golden_name = RTLIL::escape_id(args[argidx++]);
            worker.approximate_name = RTLIL::escape_id(args[argidx++]);
            worker.axmiter_name = RTLIL::escape_id(args[argidx++]);
            worker.run(design);

            log_pop();
        }
    } AxMiterPass;
} // namespace yosys_als
