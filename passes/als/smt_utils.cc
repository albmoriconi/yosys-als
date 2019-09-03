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
 * @file
 * @brief Generic utility functions for Yosys ALS module
 */

#include "smt_utils.h"

#include <boost/dynamic_bitset.hpp>

#include <cstdarg>
#include <string>
#include <vector>

namespace yosys_als {

    unsigned ceil_log2(const unsigned int x) {
#if defined(__GNUC__)
        return x > 1 ? (8 * sizeof(x)) - __builtin_clz(x - 1) : 0;
#else
        if (x > 1) {
            for (size_t i = 0; i < 8 * sizeof(x); i++) {
                if (((x - 1) >> i) == 0)
                    return i;
            }
        }

        return 0;
#endif
    }

    boost::dynamic_bitset<> truth_table_column(const size_t i, const size_t num_vars, const bool p) {
        boost::dynamic_bitset<> bs(1u << num_vars);

        for (size_t t = 0; t < bs.size(); t++)
            bs[t] = truth_table_value(i, t) == p;

        return bs;
    }

    size_t hamming_distance(const boost::dynamic_bitset<> &bs1, const boost::dynamic_bitset<> &bs2) {
        if (bs1.size() != bs2.size())
            throw std::invalid_argument("Hamming distance undefined for bitsets of different size.");

        size_t dist = 0;
        for (size_t i = 0; i < bs1.size(); i++) {
            if (bs1[i] != bs2[i])
                dist++;
        }

        return dist;
    }
} // namespace yosys_als
