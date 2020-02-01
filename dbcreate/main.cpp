/* -*- c++ -*-
 *  dbcreate -- Exact LUT synthesis database creator
 *
 *  Copyright (C) 2020  Alberto Moriconi <albmoriconi@gmail.com>
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
 * @brief Entry point for dbcreate
 */

#include "smtsynth.h"

#include <boost/dynamic_bitset.hpp>

#include <iostream>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

void dump_aig(const yosys_als::mig_model_t &model, std::ostream &out_file) {
    std::string fun_spec;
    boost::to_string(model.fun_spec, fun_spec);
    out_file << fun_spec << "\n";
    out_file << model.num_inputs << "\n";
    out_file << model.num_gates << "\n";
    for (const auto &el : model.s)
        out_file << el[0] << "\n" << el[1] << "\n";
    for (const auto &el : model.p)
        out_file << el[0] << "\n" << el[1] << "\n";
    out_file << model.out << "\n";
    out_file << model.out_p << "\n";
}

int main(int argc, char *argv[]) {
    // TODO Use Boost.Program_options for arguments
    if (argc != 5) {
        std::cout << "Expected 5 arguments" << std::endl;
        return 1;
    }

    size_t db_lutn = strtol(argv[1], nullptr, 10);
    size_t spec_bits = 1u << db_lutn;
    size_t max_db_size = 1u << (db_lutn * db_lutn);
    size_t db_start = strtol(argv[2], nullptr, 10);
    size_t db_end = strtol(argv[3], nullptr, 10);
    size_t n_threads = strtol(argv[4], nullptr, 10);

    if (db_end == 0 || db_end > max_db_size)
        db_end = max_db_size;

    size_t db_size = db_end - db_start;

    if (db_lutn < 2) {
        std::cout << "LUT parameter should be at least 2" << std::endl;
        return -1;
    }
    if (n_threads < 1) {
        std::cout << "Thread parameter should be at least 1" << std::endl;
        return -2;
    }
    if (db_start >= db_end) {
        std::cout << "No database entries selected" << std::endl;
        return -3;
    }

    std::cout << "Generating database for " << db_lutn << "-LUTs\n";
    std::cout << "Entries: [" << db_start << "..." << db_end - 1 << "]\n";
    std::cout << "Size: " << db_size << "\n";
    std::cout << "Threads: " << n_threads << "\n";

    yosys_als::mig_model_t database[db_size];

    std::vector<std::thread> threads;
    std::mutex print_mutex;
    std::condition_variable all_spawned;

    for (size_t t = 0; t < n_threads; t++) {
        size_t db_slice = db_size / n_threads;
        size_t t_start = db_start + t * db_slice;
        size_t t_end = t == n_threads - 1 ? db_end : t_start + db_slice;

        print_mutex.lock();
        std::cout << "\nSpawning thread " << t << "\n";
        std::cout << "Entries: [" << t_start << "..." << t_end - 1 << "]" << "\n";

        threads.emplace_back([t, t_start, t_end, spec_bits, &print_mutex, &database]() {
            print_mutex.lock();
            print_mutex.unlock();
            for (size_t i = t_start; i < t_end; i++) {
                auto spec = boost::dynamic_bitset<>(spec_bits, i);
                database[i] = yosys_als::synthesize_lut(spec, 0);

                std::string spec_s;
                boost::to_string(spec, spec_s);
                print_mutex.lock();
                std::cout << "\nThread " << t << ": ";
                std::cout << spec_s << " done with " << database[i].num_gates << " gates";
                print_mutex.unlock();
            }
        });
        print_mutex.unlock();
    }

    for (auto &t : threads)
        t.join();

    std::cout << "\n\nDatabase done\n";
    std::ofstream my_file("db.bin");
    if (!my_file.is_open()) {
        std::cout << "IO error" << std::endl;
        return 1;
    }
    for (const auto &i : database)
        dump_aig(i, my_file);
    my_file.close();
    std::cout << "Database dumped\n";
}
