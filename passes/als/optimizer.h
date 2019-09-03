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
 * @brief Optimization utility functions for Yosys ALS module
 */

#ifndef YOSYS_ALS_OPTIMIZER_H
#define YOSYS_ALS_OPTIMIZER_H

#include "graph.h"
#include "smtsynth.h"
#include "kernel/yosys.h"

namespace yosys_als {

    /**
     * @brief Evaluates the output reliability for a given LUT mapping
     * @param g A graph with the topological structure of the circuit
     * @param topological_order A topological ordering for the graph \c g
     * @param synthesized_luts The index of the synthesized LUTs
     * @param mapping A vector of the desired LUT variants, ordered as \c topological_order
     * @return The reliability of the output nodes of the graph
     */
    Yosys::dict<Yosys::IdString, double> output_reliability(const Graph &g,
            const std::vector<Vertex> &topological_order,
            const Yosys::dict<Yosys::Const, std::vector<mig_model_t>> &synthesized_luts,
            const std::vector<size_t> &mapping);
}

#endif //YOSYS_ALS_OPTIMIZER_H
