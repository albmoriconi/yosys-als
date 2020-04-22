#ifndef SMT_CONTEXT_H
#define SMT_CONTEXT_H

#include <boolector/boolector.h>
#include <boost/dynamic_bitset.hpp>

namespace yosys_als
{

	/*
     * Constants
     */
    constexpr size_t bitvec_sort_width = 8;

    /**
     * Context of the current SMT problem
     */
    struct smt_context_t {
        /// The function specification
        boost::dynamic_bitset<> fun_spec;

        /// The maximum hamming distance of the output
        unsigned int out_distance{};

        /// The solver
        Btor *btor{};

        /// The main bitvector sort
        BoolectorSort bitvec_sort{};

        /// The boolean sort
        BoolectorSort bool_sort{};

        /// Boolean false value
        BoolectorNode *bool_false{};

        /// Boolean true value
        BoolectorNode *bool_true{};

        /// The truth table entries
        std::vector<std::vector<BoolectorNode *>> b;

        /// The inputs to the Maj gates
        std::array<std::vector<std::vector<BoolectorNode *>>, 2> a;

        /// The structure of the MIG
        std::array<std::vector<BoolectorNode *>, 2> s;

        /// The polarities of the Maj gates inputs
        std::array<std::vector<BoolectorNode *>, 2> p;

        /// The output polarity of the MIG
        BoolectorNode *out_p{};
    };

    smt_context_t smt_context_new();

    void smt_context_delete(const smt_context_t &ctx);
    
	BoolectorNode *smt_context_bool(const smt_context_t &ctx, const bool val);

    unsigned int smt_context_assignment_uint(const smt_context_t &ctx, BoolectorNode *const node);

    bool smt_context_assignment_bool(const smt_context_t &ctx, BoolectorNode *const node);

	void assume_function_semantics(const smt_context_t &ctx);

};

#endif