/* Copyright (C) 2020-2023 by Arm Limited. All rights reserved. */

#ifndef LIB_ENUMUTILS_H_
#define LIB_ENUMUTILS_H_

#include <type_traits>

namespace lib {
    template<typename EnumType>
    constexpr std::underlying_type_t<EnumType> toEnumValue(EnumType e) noexcept
    {
        return static_cast<std::underlying_type_t<EnumType>>(e);
    }
}

#endif /* LIB_ENUMUTILS_H_ */
