#include "smt_context.h"

namespace yosys_als {
/**
 * @brief Initialize a new SMT solver context
 * @return A context with a new solver
 */
smt_context_t smt_context_new() {
    smt_context_t ctx;

    ctx.btor = boolector_new();
    boolector_set_opt(ctx.btor, BTOR_OPT_MODEL_GEN, 1);
    boolector_set_opt(ctx.btor, BTOR_OPT_INCREMENTAL, 1);
    boolector_set_opt(ctx.btor, BTOR_OPT_AUTO_CLEANUP, 1);

    ctx.bitvec_sort = boolector_bitvec_sort(ctx.btor, bitvec_sort_width);
    ctx.bool_sort = boolector_bitvec_sort(ctx.btor, 1);

    ctx.bool_false = boolector_int(ctx.btor, 0, ctx.bool_sort);
    ctx.bool_true = boolector_int(ctx.btor, 1, ctx.bool_sort);

    ctx.out_p = boolector_var(ctx.btor, ctx.bool_sort, nullptr);

    return ctx;
}

/**
 * @brief Releases the resources of the SMT context
 */
void smt_context_delete(const smt_context_t &ctx) {
    boolector_delete(ctx.btor);
}

/**
 * @brief SMT value for Boolean
 * @return The correct value in SMT context for given Boolean
 */
 BoolectorNode *smt_context_bool(const smt_context_t &ctx, const bool val) {
     return val ? ctx.bool_true : ctx.bool_false;
 }

 /**
  * @brief Gets a bitvector assignment as an unsigned integer
  * @return The assignment as an unsigned integer
  */
 unsigned int smt_context_assignment_uint(const smt_context_t &ctx, BoolectorNode *const node) {
     auto s = boolector_bv_assignment(ctx.btor, node);
     auto val = std::stoul(s, nullptr, 2);
     boolector_free_bv_assignment(ctx.btor, s);

     return val;
 }

/**
 * @brief Gets a bitvector assignment as a Boolean
 * @return The assignment as a Boolean
 */
bool smt_context_assignment_bool(const smt_context_t &ctx, BoolectorNode *const node) {
    auto s = boolector_bv_assignment(ctx.btor, node);
    auto val = s[0] == '1';
    boolector_free_bv_assignment(ctx.btor, s);

    return val;
}

/**
 * @brief Enforces function semantics for given specification
 * @param ctx The SMT solver context
 */
void assume_function_semantics(const smt_context_t &ctx) {
    if (ctx.out_distance == 0) {
        // Exact semantics
        for (size_t t = 0; t < ctx.fun_spec.size(); t++) {
            auto not_out_p = boolector_not(ctx.btor, ctx.out_p);
            auto func_value = smt_context_bool(ctx, ctx.fun_spec[t]);
            auto eq_rh = boolector_xor(ctx.btor, not_out_p, func_value);
            boolector_assume(ctx.btor, boolector_eq(ctx.btor, ctx.b.back()[t], eq_rh));
        }
    } else {
        // Hamming distance semantics
        std::vector<BoolectorNode *> all_the_xors;
        for (size_t t = 0; t < ctx.fun_spec.size(); t++) {
            auto not_out_p = boolector_not(ctx.btor, ctx.out_p);
            auto func_value = smt_context_bool(ctx, ctx.fun_spec[t]);
            auto xor_rh = boolector_xor(ctx.btor, not_out_p, func_value);
            auto bit_xor = boolector_xor(ctx.btor, ctx.b.back()[t], xor_rh);
            all_the_xors.push_back(boolector_uext(ctx.btor, bit_xor, bitvec_sort_width-1));
        }
        std::vector<BoolectorNode *> all_the_sums;
        if (!all_the_xors.empty())
            all_the_sums.push_back(all_the_xors[0]);
        for (size_t i = 1; i < all_the_xors.size(); i++) {
            all_the_sums.push_back(boolector_add(ctx.btor, all_the_sums.back(), all_the_xors[i]));
        }
        BoolectorNode *ulte_rh = boolector_int(ctx.btor, ctx.out_distance, ctx.bitvec_sort);
        boolector_assume(ctx.btor, boolector_ulte(ctx.btor, all_the_sums.back(), ulte_rh));
    }
}

};