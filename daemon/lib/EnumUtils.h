/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#ifndef LIB_ENUMUTILS_H_
#define LIB_ENUMUTILS_H_

#include <type_traits>

namespace lib {
    template<typename EnumType>
    constexpr typename std::underlying_type<EnumType>::type toEnumValue(EnumType e) noexcept
    {
        return static_cast<typename std::underlying_type<EnumType>::type>(e);
    }
}

#endif /* LIB_ENUMUTILS_H_ */
