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
 * @brief Optimization functions for Yosys ALS module
 */

#include "optimizer.h"

#include "graph.h"
#include "yosys_utils.h"
#include "kernel/yosys.h"

#include "boost/graph/topological_sort.hpp"
#include <core/moeoRealObjectiveVector.h>
#include <core/moeoObjectiveVectorTraits.h>
#include <core/moeoVector.h>
#include <eo>

#include <random>

USING_YOSYS_NAMESPACE

namespace yosys_als {

    /*
     * Data types
     */

    /**
     * Traits of the objective vector for the ALS optimization problem
     */
    class AlsObjectiveVectorTraits : public moeoObjectiveVectorTraits {
    public:
        /**
         * @param Returns true if the ith objective have to be minimized
         * @param i index of the objective
         */
        static bool minimizing(int i) {
            return true;
        };

        /**
         * Returns true if the ith objective have to be maximized
         * @param i index of the objective
         */
        static bool maximizing(int i) {
            return false;
        };

        /**
         * Returns the number of objectives
         */
        static unsigned int nObjectives() {
            return 2;
        };
    };

    /**
     * Objective vector for the ALS optimization problem
     */
    typedef moeoRealObjectiveVector<AlsObjectiveVectorTraits> AlsObjectiveVector;

    /**
     * Individual for the ALS optimization problem
     */
    typedef moeoVector<AlsObjectiveVector, double, double, size_t> AlsIndividual;

    /**
     * Generator for random initialization of ALS individuals
     */
    class AlsGenerator {
    public:
        AlsGenerator() : mt(std::mt19937((std::random_device())())) { }

        // NOTE Distribution is not uniform for all values of max
        size_t roll(size_t max) {
            return dist(mt) % (max + 1);
        }

    private:
        std::mt19937 mt;
        std::uniform_int_distribution<size_t> dist;
    };

    /**
     * Initialization functor for the ALS individuals
     */
    class AlsInit : public eoInit<AlsIndividual> {
    public:
        typedef typename AlsIndividual::AtomType AtomType;

        AlsInit(const Graph &g, dict<Const, std::vector<mig_model_t>> &luts, const std::vector<Vertex> &order) :
            g(g), luts(luts), order(order) {}

        void operator()(AlsIndividual& als_i) override {
            als_i.resize(order.size());

            for (size_t i = 0; i < order.size(); i++)
                als_i[i] = generator.roll(luts[get_lut_param(g[order[i]].cell)].size());

            als_i.invalidate();
        }

    private:
        static AlsGenerator generator;

        const Graph &g;
        dict<Const, std::vector<mig_model_t>> &luts;
        const std::vector<Vertex> &order;
    };

    /*
     * Exposed functions
     */

    int als_optimizer_moeo(Module *module, dict<Const, std::vector<mig_model_t>> &synthesized_luts) {
        Graph g = graph_from_module(module);
        std::vector<Vertex> topological_order;
        topological_sort(g, std::back_inserter(topological_order));

        return 0;
    }
}
