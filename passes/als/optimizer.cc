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
#include "optimizer_utils.h"
#include "kernel/yosys.h"

#include <boost/graph/topological_sort.hpp>
#include <boost/range/adaptor/indexed.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <core/moeoEvalFunc.h>
#include <core/moeoRealObjectiveVector.h>
#include <core/moeoObjectiveVectorTraits.h>
#include <core/moeoVector.h>
#include <Eigen/Dense>
#include <eo>

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
                als_i[i] = rng.random((luts[get_lut_param(g[order[i]].cell)].size()));

            als_i.invalidate();
        }

    private:
        const Graph &g;
        dict<Const, std::vector<mig_model_t>> &luts;
        const std::vector<Vertex> &order;
    };

    /**
     * Evaluator for an ALS individual
     */
    class AlsEval : public moeoEvalFunc<AlsIndividual> {
    public:
        AlsEval(const Graph &g, dict<Const, std::vector<mig_model_t>> &luts, const std::vector<Vertex> &order) :
            g(g), luts(luts), order(order) {}

        void operator()(AlsIndividual &als_i) override {
            AlsObjectiveVector obj_v;
            obj_v[0] = 1.0 - circuit_reliability(output_reliability(als_i));
            obj_v[1] = gates_count(als_i);
            als_i.objectiveVector(obj_v);
        }

    private:
        const Graph &g;
        dict<Const, std::vector<mig_model_t>> &luts;
        const std::vector<Vertex> &order;

        dict<IdString, double> output_reliability(const AlsIndividual &als_i) {
            dict<IdString, double> rel;
            dict<vertex_t, Eigen::Matrix2d> z_matrix_for;

            for (auto const &v : boost::adaptors::reverse(order) | boost::adaptors::indexed(0)) {
                // Primary inputs and constants
                if (boost::in_degree(v.value(), g) == 0) {
                    z_matrix_for[g[v.value()]] = z_in_degree_0(g, v.value());
                } else { // Other vertices (i.e. cells)
                    auto &cell_function = get_lut_param(g[v.value()].cell);
                    auto &cell_synth_lut = luts[cell_function];
                    z_matrix_for[g[v.value()]] = z_in_degree_pos(g, v.value(), z_matrix_for, cell_synth_lut[0],
                                                                 cell_synth_lut[als_i[v.index()]]);

                    // Cells connected to primary outputs
                    if (boost::out_degree(v.value(), g) == 0)
                        rel[g[v.value()].name] = reliability_from_z(z_matrix_for[g[v.value()]]);
                }
            }

            return rel;
        }

        static double circuit_reliability(const dict<IdString, double> &all_the_rels) {
            double c_rel = 1.0;

            for (auto &a_rel : all_the_rels)
                c_rel *= a_rel.second;

            return c_rel;
        }

        size_t gates_count(const AlsIndividual &als_i) {
            size_t count = 0;

            for (size_t i = 0; i < als_i.size(); i++) {
                count += luts[get_lut_param(g[order[i]].cell)][als_i[i]].num_gates;
            }

            return count;
        }
    };

    /**
     * Quadratic crossover operator for ALS individual
     */
    class AlsOpCrossoverQuad : public eoQuadOp<AlsIndividual> {
    public:
        bool operator()(AlsIndividual &als_i1, AlsIndividual &als_i2) override {
            bool doneSomething = false;
            size_t p1 = rng.random(std::min(als_i1.size(), als_i2.size()));
            size_t p2 = rng.random(std::min(als_i1.size(), als_i2.size()));

            if (p1 > p2)
                std::swap(p1, p2);

            AlsIndividual off1 = generateOffspring(als_i1, als_i2, p1, p2);
            AlsIndividual off2 = generateOffspring(als_i2, als_i1, p1, p2);

            if ((als_i1 != off1) || (als_i2 != off2)) {
                als_i1.value(off1);
                als_i2.value(off2);
                doneSomething = true;
            }

            return doneSomething;
        }

    private:
        static AlsIndividual generateOffspring(const AlsIndividual &als_i1, const AlsIndividual &als_i2,
                const size_t p1, const size_t p2) {
            AlsIndividual off = als_i1;

            for (size_t i = 0; i < p1; i++)
                off[i] = als_i1[i];
            for (size_t i = p1; i < p2; i++)
                off[i] = als_i2[i];
            for (size_t i = p2; i < als_i1.size(); i++)
                off[i] = als_i1[i];

            return off;
        }
    };

    class AlsOpMutation : public eoMonOp<AlsIndividual> {
    public:
        AlsOpMutation(const Graph &g, dict<Const, std::vector<mig_model_t>> &luts, const std::vector<Vertex> &order) :
                g(g), luts(luts), order(order) {}

        bool operator()(AlsIndividual &als_i) override {
            // TODO Make it mutate more, depending on size of chromosome (maybe mutate in a neighborhood?)
            bool done_something = false;
            size_t p = rng.random(als_i.size());
            size_t new_val = rng.random((luts[get_lut_param(g[order[p]].cell)].size()));

            if (als_i[p] != new_val) {
                als_i[p] = new_val;
                done_something = true;
            }

            return done_something;
        }

    private:
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
