#ifndef YOSYS_ALS_LUT_CATALOG_H
#define YOSYS_ALS_LUT_CATALOG_H

#include "aig_model.h"
#include "yosys_utils.h"
#include "kernel/yosys.h"
#include <string>
#include <vector>

USING_YOSYS_NAMESPACE

namespace yosys_als {

    struct lut_catalog_t {
	private:
        /// Index of the synthesized LUTs
        dict<Const, std::vector<aig_model_t>> synthesized_luts;

    public:  
        void generate_lut_variants(Module * const, bool);
 		
		std::vector<aig_model_t>& at(const Yosys::Const &key) {return synthesized_luts.at(key);}
		const std::vector<aig_model_t>& at(const Yosys::Const &key) const {return synthesized_luts.at(key);}
		std::vector<aig_model_t>& operator[](const Yosys::Const &key) {return synthesized_luts[key];}

		friend std::ofstream& operator<<(std::ofstream&, const lut_catalog_t&);
		friend std::ifstream& operator>>(std::ifstream&, lut_catalog_t&);

    };

}
#endif //YOSYS_ALS_LUT_CATALOG_H
