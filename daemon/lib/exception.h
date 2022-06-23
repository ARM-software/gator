/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include <exception>
#include <stdexcept>

namespace lib {
    /**
     * Get the descriptive string from some exception pointer
     *
     * @param ptr The expection pointer
     * @return Some descriptive string (usually std::exception::what())
     */
    inline char const * get_exception_ptr_str(std::exception_ptr ptr)
    {
        if (ptr == nullptr) {
            return "<nullptr>";
        }
        try {
            std::rethrow_exception(ptr);
        }
        catch (std::exception const & ex) {
            return ex.what();
        }
        catch (std::string const & str) {
            return str.c_str();
        }
        catch (char const * str) {
            if (str != nullptr) {
                return str;
            }
            return "<null cstr>";
        }
        catch (...) {
            return "<unknown>";
        }
    }
}
