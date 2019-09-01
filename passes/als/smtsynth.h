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

namespace yosys_als {

    /**
     * @brief An MIG model
     * The first \c num_inputs entries of \c s are the MIG primary inputs.
     * The first input is always the constant zero.
     * The output of the last entry of \c s is the MIG primary output.
     */
    struct mig_model_t {
        /// Number of inputs to the model
        size_t num_inputs;

        /// Number of gates in the model
        size_t num_gates;

        /// Variables in the model
        std::vector<std::array<size_t, 3>> s;

        /// Polarities of variables in the model
        std::vector<std::array<bool, 3>> p;

        /// Output variable
        size_t out;

        /// Polarity of the output
        bool out_p;
    };

    /**
     * @brief SMT AIG exact synthesis for given function specification
     * @param fun_spec The function specification
     * @param out_distance The maximum hamming distance of the synthesized function
     * @return The synthesized AIG model
     */
    mig_model_t synthesize_lut(const boost::dynamic_bitset<> &fun_spec, unsigned int out_distance);
} // namespace yosys_als

#endif //YOSYS_ALS_SMTSYNTH_H
