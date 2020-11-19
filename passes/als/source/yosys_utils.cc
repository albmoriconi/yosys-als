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

#include "yosys_utils.h"

#include "smtsynth.h"

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/array.hpp>

#include <mutex>
#include <random>

USING_YOSYS_NAMESPACE

namespace boost {
    namespace serialization {

        template<class Archive>
        void serialize(Archive &ar, yosys_als::aig_model_t &aig,
                const unsigned int version __attribute__((unused))) {
            ar & aig.fun_spec;
            ar & aig.num_inputs;
            ar & aig.num_gates;
            ar & aig.s;
            ar & aig.p;
            ar & aig.out;
            ar & aig.out_p;
        }

        template <typename Ar, typename Block, typename Alloc>
        void save(Ar& ar, dynamic_bitset<Block, Alloc> const& bs, unsigned) {
            size_t num_bits = bs.size();
            std::vector<Block> blocks(bs.num_blocks());
            to_block_range(bs, blocks.begin());

            ar & num_bits & blocks;
        }

        template <typename Ar, typename Block, typename Alloc>
        void load(Ar& ar, dynamic_bitset<Block, Alloc>& bs, unsigned) {
            size_t num_bits;
            std::vector<Block> blocks;
            ar & num_bits & blocks;

            bs.resize(num_bits);
            from_block_range(blocks.begin(), blocks.end(), bs);
            bs.resize(num_bits);
        }

        template <typename Ar, typename Block, typename Alloc>
        void serialize(Ar& ar, dynamic_bitset<Block, Alloc>& bs, unsigned version) {
            split_free(ar, bs, version);
        }

    }
}

namespace yosys_als {

    std::default_random_engine rng{std::random_device{}()};
    std::mutex db_mtx;
    std::mutex log_mtx;

    struct aig_bundle_t {
        aig_model_t *aig;
        bool hit;
    };

    // TODO Needs refactoring
    aig_model_t synthesize_lut(const Const &lut, unsigned int out_distance = 0,
            bool debug = false, sqlite3 *db = nullptr) {

        // Database initialization
        if (db != nullptr) {
            db_mtx.lock();
            if (sqlite3_table_column_metadata(db, 0, "luts", 0, 0, 0, 0, 0, 0) != SQLITE_OK) {
                std::string query = "create table luts (spec text not null, aig blob not null, primary key (spec));";
                sqlite3_exec(db, query.c_str(), 0, 0, 0);
                if (debug)
                    log("Initialized cache\n");
            }
            db_mtx.unlock();
        }

        if (debug) {
            log_mtx.lock();
            log("[SYNTH] Requested synthesis for %s@%d.\n", lut.as_string().c_str(), out_distance);
            log_mtx.unlock();
        }

        aig_model_t aig;
        aig_bundle_t aig_bundle {&aig, false};
        string fun_spec;

        // Cache lookup
        if (db != nullptr) {
            std::string key = lut.as_string() + "@" + std::to_string(out_distance);
            std::string query = "select aig from luts where spec = '" + key + "';";
            sqlite3_exec(db, query.c_str(),
                    [](void *aig_bundle, int argc, char **argv, char **azColName) { // Cache hit
                        (void) argc;
                        (void) azColName;

                        auto *the_bundle = (aig_bundle_t*) aig_bundle;
                        std::istringstream is(argv[0]);
                        boost::archive::text_iarchive ia(is);

                        ia >> *(the_bundle->aig);
                        the_bundle->hit = true;

                        return 0;
                    }, (void *) &aig_bundle, 0);

            if (!aig_bundle.hit) { // Cache miss, synth and insert in cache
                std::ostringstream os;
                boost::archive::text_oarchive oa(os);

                log_mtx.lock();
                log("[CACHE] Cache miss for %s.\n", key.c_str());
                log_mtx.unlock();

                aig = yosys_als::synthesize_lut(boost::dynamic_bitset<>(lut.as_string()), out_distance);
                oa << aig;

                std::string query_ins = "insert into luts values ('" + key + "', '" + os.str() + "');";
                sqlite3_exec(db, query_ins.c_str(), 0, 0, 0);
                boost::to_string(aig.fun_spec, fun_spec);
                std::string key_replace = fun_spec + "@0";
                query_ins = "insert into luts values ('" + key_replace + "', '" + os.str() + "');";
                sqlite3_exec(db, query_ins.c_str(), 0, 0, 0);
            } else {
                log_mtx.lock();
                log("[CACHE] Cache hit for %s.\n", key.c_str());
                log_mtx.unlock();
            }
        } else {
            aig = yosys_als::synthesize_lut(boost::dynamic_bitset<>(lut.as_string()), out_distance);
        }

        if (debug) {
            boost::to_string(aig.fun_spec, fun_spec);
            log_mtx.lock();
            log("[SAT] Satisfied %s@%d with %zu gates, implements %s.\n",
                    lut.as_string().c_str(), out_distance, aig.num_gates, fun_spec.c_str());
            log_mtx.unlock();
        }

        return aig;
    }
}
