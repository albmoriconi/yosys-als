/* -*- c++ -*-
 *  yosys-als -- Approximate logic synthesis
 *
 *  Copyright (C) 2019  Alberto Moriconi <albmoriconi@gmail.com>
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

/**
 * @file
 * @brief RTLIL and Yosys utility functions for Yosys ALS module
 */

#include "yosys_utils.h"

#include "smtsynth.h"

USING_YOSYS_NAMESPACE

namespace yosys_als {

    aig_model_t synthesize_lut(const Const &lut, unsigned int out_distance, bool debug) {
        if (debug)
            log("%s @delta%d... ", lut.as_string().c_str(), out_distance);

        auto aig = yosys_als::synthesize_lut(boost::dynamic_bitset<>(lut.as_string()), out_distance);

        if (debug) {
            string fun_spec;
            boost::to_string(aig.fun_spec, fun_spec);
            log("satisfied with %zu gates, implements %s.\n", aig.num_gates, fun_spec.c_str());
        }

        return aig;
    }
}
