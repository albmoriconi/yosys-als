//
// Created by alberto on 25/01/20.
//

#include <string>
#include <iostream>
#include <fstream>
#include <bitset>
#include <thread>
#include <boost/dynamic_bitset.hpp>
#include "smtsynth.h"

constexpr size_t db2_size = 16;
constexpr size_t db3_size = 512;
constexpr size_t db4_size = 65536;

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

int main() {
    yosys_als::mig_model_t database2[db2_size];
    yosys_als::mig_model_t database3[db3_size];
    yosys_als::mig_model_t database4[db4_size];

//    for (size_t i = 0; i < db2_size; i++) {
//        std::string spec_str = std::bitset<4>(i).to_string();
//        //std::cout << "Synthesizing " << spec_str << "... ";
//        database2[i] = yosys_als::synthesize_lut(boost::dynamic_bitset<>(spec_str), 0);
//        //std::cout << "done with " << database2[i].num_gates << " gates\n";
//    }
//
//    std::cout << "LUT2 database done\n";
//    std::ofstream my_file2("db2.bin");
//    if (!my_file2.is_open()) {
//        std::cout << "error" << std::endl;
//        return 1;
//    }
//    for (const auto &i : database2)
//        dump_aig(i, my_file2);
//    my_file2.close();
//    std::cout << "LUT2 database dumped\n";
//
//    for (size_t i = 0; i < db3_size; i++) {
//        std::string spec_str = std::bitset<8>(i).to_string();
//        //std::cout << "Synthesizing " << spec_str << "... ";
//        database3[i] = yosys_als::synthesize_lut(boost::dynamic_bitset<>(spec_str), 0);
//        //std::cout << "done with " << database3[i].num_gates << " gates\n";
//    }
//
//    std::cout << "LUT3 database done\n";
//    std::ofstream my_file3("db3.bin");
//    if (!my_file3.is_open()) {
//        std::cout << "error" << std::endl;
//        return 1;
//    }
//    for (const auto &i : database3)
//        dump_aig(i, my_file3);
//    my_file3.close();
//    std::cout << "LUT3 database dumped\n";

    std::thread t1([&]() {
        for (size_t i = 0; i < db4_size; i += 8) {
            std::string spec_str = std::bitset<16>(i).to_string();
            std::cout << "Synthesizing " << spec_str << "... ";
            database4[i] = yosys_als::synthesize_lut(boost::dynamic_bitset<>(spec_str), 0);
            std::cout << "done with " << database4[i].num_gates << " gates\n";
        }
    });

    std::thread t2([&]() {
        for (size_t i = 2; i < db4_size; i += 8) {
            std::string spec_str = std::bitset<16>(i).to_string();
            std::cout << "Synthesizing " << spec_str << "... ";
            database4[i] = yosys_als::synthesize_lut(boost::dynamic_bitset<>(spec_str), 0);
            std::cout << "done with " << database4[i].num_gates << " gates\n";
        }
    });

    std::thread t3([&]() {
        for (size_t i = 4; i < db4_size; i += 8) {
            std::string spec_str = std::bitset<16>(i).to_string();
            std::cout << "Synthesizing " << spec_str << "... ";
            database4[i] = yosys_als::synthesize_lut(boost::dynamic_bitset<>(spec_str), 0);
            std::cout << "done with " << database4[i].num_gates << " gates\n";
        }
    });

    std::thread t4([&]() {
        for (size_t i = 6; i < db4_size; i += 8) {
            std::string spec_str = std::bitset<16>(i).to_string();
            std::cout << "Synthesizing " << spec_str << "... ";
            database4[i] = yosys_als::synthesize_lut(boost::dynamic_bitset<>(spec_str), 0);
            std::cout << "done with " << database4[i].num_gates << " gates\n";
        }
    });

    t1.join();
    t2.join();
    t3.join();
    t4.join();

    std::cout << "LUT4 database done\n";
    std::ofstream my_file4("db4.bin");
    if (!my_file4.is_open()) {
        std::cout << "error" << std::endl;
        return 1;
    }
    for (const auto &i : database4)
        dump_aig(i, my_file4);
    my_file4.close();
    std::cout << "LUT4 database dumped\n";
}
