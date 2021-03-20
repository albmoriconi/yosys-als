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

#ifndef YOSYS_ALS_YOSYS_UTILS_H
#define YOSYS_ALS_YOSYS_UTILS_H

#include "smtsynth.h"

#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "kernel/yosys.h"

#if defined __GNUC__
#pragma GCC diagnostic pop
#endif

#include <sqlite3.h>

namespace yosys_als {

/// Type for the catalogue of synthesized LUTs
typedef Yosys::dict<Yosys::Const, std::vector<aig_model_t>> lut_catalogue_t;

/**
 * @brief Wrapper for \c synthesize_lut
 * @param lut The LUT specification
 * @param out_distance The approximation degree
 * @return The synthesized AIG model
 */
aig_model_t synthesize_lut(const Yosys::Const &lut, unsigned int out_distance, unsigned int max_tries, bool debug, sqlite3 *db);

/**
 * Checks if cell is a LUT
 * @param cell A cell
 * @return \c true if the cell is a LUT, otherwise \c false
 */
inline bool is_lut(const Yosys::Cell *const cell) {
    return cell->hasParam("\\LUT");
}

/**
 * Gets the value of the \c LUT parameter of a cell, i.e. its specification
 * @param cell A cell
 * @return The value of the \c LUT parameter of the cell
 */
inline const Yosys::Const &get_lut_param(const Yosys::Cell *const cell) {
    return cell->getParam("\\LUT");
}
}

#endif //YOSYS_ALS_YOSYS_UTILS_H
