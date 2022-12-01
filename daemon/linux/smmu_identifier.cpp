/* Copyright (C) 2022 by Arm Limited. All rights reserved. */
#include "linux/smmu_identifier.h"

#include <boost/regex.hpp>

namespace gator::smmuv3 {

    smmuv3_identifier_t::smmuv3_identifier_t(std::string_view value)
    {
        auto pattern = boost::regex("^(0[xX])?([0-9a-fA-F]{3})(_|[0-9a-fA-F]{2})([0-9a-fA-F]{3})$");
        boost::cmatch match;
        if (boost::regex_match(value.begin(), value.end(), match, pattern)) {
            category = category_t::iidr;
            data = iidr_t({match[2].str(), match[3].str(), match[4].str()});
        }
        else {
            category = category_t::model_name;
            data = std::string(value);
        }
    }

}
