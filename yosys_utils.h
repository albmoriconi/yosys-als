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
 * @brief RTLIL and Yosys utility functions for Yosys ALS module
 */

#ifndef YOSYS_ALS_YOSYS_UTILS_H
#define YOSYS_ALS_YOSYS_UTILS_H

#include "smtsynth.h"
#include "kernel/yosys.h"

namespace yosys_als {

    /**
     * @brief Wrapper for \c synthesize_lut
     * @param lut The LUT specification
     * @param ax_degree The approximation degree
     * @return The synthesized AIG model
     */
    aig_model_t synthesize_lut(const Yosys::Const &lut, unsigned int ax_degree, bool debug);

    /**
     * @brief Applies LUT-to-AIG mapping to module
     * @param module The module (modified in place)
     * @param mapping The LUT-to-AIG mapping
     */
    void apply_mapping(Yosys::Module *module, const Yosys::dict<Yosys::IdString, aig_model_t> &mapping, bool debug);

    /**
     * Checks if cell is a LUT
     * @param cell A cell
     * @return \c true if the cell is a LUT, otherwise \c false
     */
    static constexpr bool is_lut(const Yosys::Cell *const cell) {
        return cell->hasParam("\\LUT");
    }

    /**
     * Gets the value of the \c LUT parameter of a cell, i.e. its specification
     * @param cell A cell
     * @return The value of the \c LUT parameter of the cell
     */
    static constexpr const Yosys::Const &get_lut_param(const Yosys::Cell *const cell) {
        return cell->getParam("\\LUT");
    }

    /**
     * @brief Counts the number of cells in the module
     * @param module A module
     * @param type The type of cells to count
     * @return The number of cells of the specified type, or all cells if unspecified
     */
    size_t count_cells(const Yosys::Module *module, const Yosys::IdString &type = Yosys::IdString());

    /**
     * @brief Copies a module and adds it to the same design
     * @param source The module to copy
     * @param copy_id The name of the copy
     * @return A pointer to the created copy
     */
    Yosys::Module *cloneInSameDesign(const Yosys::Module *source, const Yosys::IdString &copy_id);

    /**
     * @brief Cleans dangling cells and wires in the module and functionally reduces its nodes
     * @param module A module
     */
    void clean_and_freduce(Yosys::Module *module);

    /**
     * @brief Checks a SAT problem in the module
     * @param module A module
     * @return \c true if the problem is SAT, else \c false
     */
    bool checkSat(Yosys::Module *module);

    /**
     * @brief Replaces a LUT in the module
     * @param module A module
     * @param lut A substitution
     */
    void replace_lut(Yosys::Module *module, const Yosys::pair<Yosys::IdString, aig_model_t> &lut);
}

#endif //YOSYS_ALS_YOSYS_UTILS_H
