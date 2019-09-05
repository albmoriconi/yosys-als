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
#include <archive/moeoUnboundedArchive.h>
#include <utils/moeoArchiveUpdater.h>
#include <core/moeoVector.h>
#include <fitness/moeoDominanceDepthFitnessAssignment.h>
#include <diversity/moeoFrontByFrontCrowdingDiversityAssignment.h>
#include <comparator/moeoFitnessThenDiversityComparator.h>
#include <replacement/moeoElitistReplacement.h>
#include <algo/moeoEasyEA.h>
#include <selection/moeoDetTournamentSelect.h>
#include <do/make_pop.h>

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

            for (auto const &v : boost::adaptors::reverse(order) | boost::adaptors::indexed(0)) {
                // TODO We need this because PIs are in the genotype; maybe this should be changed
                Cell *cell = g[v.value()].cell;

                //if (cell)
                //    als_i[v.index()] = rng.random((luts[get_lut_param(cell)].size()));
                //else
                    als_i[v.index()] = 0;
            }

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

            for (auto const &v : boost::adaptors::reverse(order) | boost::adaptors::indexed(0)) {
                // TODO We need this because PIs are in the genotype; maybe this should be changed
                Cell *cell = g[v.value()].cell;

                if (cell)
                    count += luts[get_lut_param(cell)][als_i[v.index()]].num_gates;
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

            // TODO We need this because PIs are in the genotype; maybe this should be changed
            // TODO Maybe we should have reversed the topological order at the beginning...
            Cell *cell = g[order[als_i.size() - p - 1]].cell;

            if (cell) {
                size_t new_val = rng.random((luts[get_lut_param(cell)].size()));

                if (als_i[p] != new_val) {
                    als_i[p] = new_val;
                    done_something = true;
                }
            }

            return done_something;
        }

    private:
        const Graph &g;
        dict<Const, std::vector<mig_model_t>> &luts;
        const std::vector<Vertex> &order;
    };

    /*
     * Utility functions
     */

    eoEvalFuncCounter<AlsIndividual> &do_make_eval(eoParser &parser, eoState &state,
            const Graph &g, dict<Const, std::vector<mig_model_t>> &luts, const std::vector<Vertex> &order) {
       auto *plainEval = new AlsEval(g, luts, order);
       auto *eval = new eoEvalFuncCounter<AlsIndividual>(*plainEval);
       state.storeFunctor(eval);

       return *eval;
     }

    eoInit<AlsIndividual> &do_make_genotype(eoParser& parser, eoState& state,
            const Graph &g, dict<Const, std::vector<mig_model_t>> &luts, const std::vector<Vertex> &order) {
      auto *init = new AlsInit(g, luts, order);
      state.storeFunctor(init);

      return *init;
    }

    eoGenOp<AlsIndividual> &do_make_op(eoParameterLoader& parser, eoState& state,
            const Graph &g, dict<Const, std::vector<mig_model_t>> &luts, const std::vector<Vertex> &order) {
        // Crossover
        eoQuadOp<AlsIndividual> *cross = new AlsOpCrossoverQuad;
        state.storeFunctor(cross);
        double crossRate = parser.createParam(1.0, "crossRate",
                "Relative rate for the only crossover", 0, "Variation Operators").value();
        auto *propXover = new eoPropCombinedQuadOp<AlsIndividual>(*cross, crossRate);
        state.storeFunctor(propXover);

        // Mutation
        eoMonOp<AlsIndividual> *mut = new AlsOpMutation(g, luts, order);
        state.storeFunctor(mut);
        double mutRate = parser.createParam(1.0, "mutRate",
                "Relative rate for mutation", 0, "Variation Operators").value();
        auto *propMutation = new eoPropCombinedMonOp<AlsIndividual>(*mut, mutRate);
        state.storeFunctor(propMutation);

        // First read the individual level parameters
        eoValueParam<double> &pCrossParam = parser.createParam(0.5, "pCross",
                "Probability of Crossover", 'c', "Variation Operators" );
        if ((pCrossParam.value() < 0) || (pCrossParam.value() > 1))
            throw std::runtime_error("Invalid pCross");
        eoValueParam<double>& pMutParam = parser.createParam(0.75, "pMut",
                "Probability of Mutation", 'm', "Variation Operators" );
        if ((pMutParam.value() < 0) || (pMutParam.value() > 1))
            throw std::runtime_error("Invalid pMut");

        // Crossover with correct probability
        eoProportionalOp<AlsIndividual> *propOp = new eoProportionalOp<AlsIndividual>;
        state.storeFunctor(propOp);
        eoQuadOp<AlsIndividual> *ptQuad = new eoQuadCloneOp<AlsIndividual>;
        state.storeFunctor(ptQuad);
        propOp->add(*propXover, pCrossParam.value()); // Crossover, with P pcross
        propOp->add(*ptQuad, 1-pCrossParam.value()); // Nothing, with P 1-pcross

        // Mutation with correct probability
        eoSequentialOp<AlsIndividual> *op = new eoSequentialOp<AlsIndividual>;
        state.storeFunctor(op);
        op->add(*propOp, 1.0); // Always do combined crossover (it already has its P)
        op->add(*propMutation, pMutParam.value()); // Then mutation, with P pmut

        return *op;
    }

    /*
     * Exposed functions
     */

    int als_optimizer_moeo(Module *module, dict<Const, std::vector<mig_model_t>> &synthesized_luts) {
        Graph g = graph_from_module(module);
        std::vector<Vertex> topological_order;
        topological_sort(g, std::back_inserter(topological_order));

        // TODO Tune the representation-independent parameters
        try {
            char **argv = new char *[1];
            char *arg = new char[2];
            arg[0] = 'a';
            arg[1] = '\0';
            argv[0] = arg;
            eoParser parser(1, argv);
            unsigned int popSize = parser.createParam((unsigned int) (100), "popSize",
                    "Population Size", 'P', "Evolution Engine" ).value();
            eoState state;

            /*** the representation-dependent things ***/
            // The fitness evaluation
            eoEvalFuncCounter<AlsIndividual> &eval = do_make_eval(parser, state, g, synthesized_luts, topological_order);
            // the genotype (through a genotype initializer)
            eoInit<AlsIndividual> &init = do_make_genotype(parser, state, g, synthesized_luts, topological_order);
            // the variation operators
            eoGenOp<AlsIndividual> &op = do_make_op(parser, state, g, synthesized_luts, topological_order);


            /*** the representation-independent things ***/

            // initialization of the population
            eoPop<AlsIndividual> &pop = do_make_pop(parser, state, init);
            // definition of the archive
            moeoUnboundedArchive<AlsIndividual> arch;
            // stopping criteria
            unsigned int maxGen = parser.createParam((unsigned int) (100), "maxGen",
                    "Maximum number of gen.", 'G', "Stopping criterion").value();

            eoGenContinue<AlsIndividual> term(maxGen);
            // checkpointing
            eoCheckPoint<AlsIndividual> checkpoint(term);
            moeoArchiveUpdater<AlsIndividual> updater(arch, pop);
            checkpoint.add(updater);
            // fitness assignment
            moeoDominanceDepthFitnessAssignment<AlsIndividual> fitnessAssignment;
            // diversity preservation
            moeoFrontByFrontCrowdingDiversityAssignment<AlsIndividual> diversityAssignment;
            // comparator
            moeoFitnessThenDiversityComparator<AlsIndividual> comparator;
            // selection scheme
            moeoDetTournamentSelect<AlsIndividual> select(comparator, 2);
            // replacement scheme
            moeoElitistReplacement<AlsIndividual> replace(fitnessAssignment, diversityAssignment, comparator);
            // breeder
            eoGeneralBreeder<AlsIndividual> breed(select, op);
            // algorithm
            moeoEasyEA<AlsIndividual> algo(checkpoint, eval, breed, replace, fitnessAssignment,
                    diversityAssignment);


            /*** Go ! ***/
            // help ?
            make_help(parser);

            // first evalution (for printing)
            apply<AlsIndividual>(eval, pop);

            // printing of the initial population
            std::cout << "Initial Population\n";
            pop.sortedPrintOn(std::cout);
            std::cout << std::endl;

            // run the algo
            algo(pop);

            // printing of the final population
            std::cout << "Final Population\n";
            pop.sortedPrintOn(std::cout);
            std::cout << std::endl;

            // printing of the final archive
            std::cout << "Final Archive\n";
            arch.sortedPrintOn(std::cout);
            std::cout << std::endl;


        }
        catch (std::exception &e) {
            std::cout << e.what() << std::endl;
        }

        return 0;
    }
}
