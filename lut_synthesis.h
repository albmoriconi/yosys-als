//
// Created by Alberto Moriconi on 2019-08-22.
//

#ifndef YOSYS_ALS_LUT_SYNTHESIS_H
#define YOSYS_ALS_LUT_SYNTHESIS_H

#include <string>
#include <utility>
#include <vector>

namespace smt {
    struct aig_model {
        size_t num_inputs;

        std::vector<std::pair<int, int>> s;
        std::vector<std::pair<bool, bool>> p;
    };

    aig_model lut_synthesis(const std::string &fun_spec, int ax_degree);
}

#endif //YOSYS_ALS_LUT_SYNTHESIS_H
