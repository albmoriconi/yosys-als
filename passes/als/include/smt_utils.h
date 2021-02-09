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
 * @brief Generic utility functions for Yosys ALS module
 */

#ifndef YOSYS_ALS_SMT_UTILS_H
#define YOSYS_ALS_SMT_UTILS_H

#include <boost/dynamic_bitset.hpp>

#include <cstdarg>
#include <string>

namespace yosys_als {

/*
 * Numeric utility functions
 */

/**
 * @brief Check if power of two
 * @param x An unsigned integer
 * @return \c true if \c v is a power of 2, otherwise \c false
 */
constexpr bool is_power_of_2(const unsigned int x) {
    return x && ((x & (x - 1)) == 0);
}

/**
 * @brief Fast ceil log2
 * @param x An unsigned integer
 * @return The ceil log2 of \c x
 */
unsigned int ceil_log2(unsigned int x);

/*
 * Truth tables utility functions
 */

/**
 * @brief Truth table value
 * @param i Variable index
 * @param t Row of the table
 * @return The value of given variable (variable 0 is always \c false)
 */
inline bool truth_table_value(const size_t i, const size_t t) {
    if (i == 0)
        return false;

    return t % (1u << i) >= (1u << (i - 1));
}

/**
 * @brief Return a column of the truth table
 * @param col The number of the column
 * @param num_vars The number of variables
 * @param p The polarity of the column
 * @return The column of the truth table (column 0 is all 0s)
 */
boost::dynamic_bitset<> truth_table_column(size_t i, size_t num_vars, bool p);

/**
 * @brief Hamming distance between two bitsets
 * @param bs1 A bitset
 * @param bs2 A bitset
 * @return The hamming distance between \c bs1 and \c bs2
 */
size_t hamming_distance(const boost::dynamic_bitset<> &bs1, const boost::dynamic_bitset<> &bs2);
} // namespace yosys_als

#endif //YOSYS_ALS_SMT_UTILS_H
