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

/**
 * \file
 * \brief SMT synthesis for Yosys ALS module
 */

#ifndef YOSYS_ALS_SMTSYNTH_H
#define YOSYS_ALS_SMTSYNTH_H

#include <boost/dynamic_bitset.hpp>

#include <array>
#include <string>
#include <vector>

/**
 * \brief SMT AIG synthesis namespace
 */
namespace yosys_als {

    /**
     * \brief An AIG model
     * The first \c num_inputs entries of \c s are the AIG primary inputs.
     * The first input is always the constant zero.
     * The output of the last entry of \c s is the AIG primary output.
     */
    struct aig_model_t {
        /// Number of inputs to the model
        int num_inputs;

        /// Number of gates in the model
        int num_gates;

        /// Variables in the model
        std::vector<std::array<int, 2>> s;

        /// Polarities of variables in the model
        std::vector<std::array<int, 2>> p;

        /// Output variable
        int out;

        /// Polarity of the output
        int out_p;
    };

    /**
     * \brief SMT AIG exact synthesis for given function specification
     * @param fun_spec The function specification
     * @param ax_degree The maximum bit-distance of the synthesized function
     * @return The synthesized AIG model
     */
    aig_model_t lut_synthesis(const boost::dynamic_bitset<> &fun_spec, unsigned ax_degree);
} // namespace yosys_als

#endif //YOSYS_ALS_SMTSYNTH_H
