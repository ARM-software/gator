/* Copyright (C) 2017-2023 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_COUNTERHELPERS_H
#define INCLUDE_NON_ROOT_COUNTERHELPERS_H

#include <type_traits>
#include <utility>

namespace non_root {
    /**
     * Helper object to track the value of an absolute counter
     */
    template<typename T>
    class AbsoluteCounter {
    public:
        using value_type = T;
        using cref_type = std::conditional_t<std::is_integral_v<T> || std::is_floating_point_v<T>, T, const T &>;

        AbsoluteCounter() : currentValue_(), changed_(true) {}

        cref_type value() const { return currentValue_; }

        bool changed() const { return changed_; }

        void done() { changed_ = false; }

        void update(cref_type v)
        {
            changed_ |= (currentValue_ != v);
            currentValue_ = v;
        }

    private:
        value_type currentValue_;
        bool changed_;
    };

    /**
     * Helper object to track the value of an delta counter
     */
    template<typename T>
    class DeltaCounter {
    public:
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>, "T must be integral or float type");

        using value_type = T;

        DeltaCounter() : currentValue(), newValue() {}

        void update(value_type v) { newValue = v; }

        value_type delta() const { return newValue - currentValue; }

        void done() { currentValue = newValue; }

        bool changed() const { return newValue != currentValue; }

    private:
        value_type currentValue;
        value_type newValue;
    };
}

#endif /* INCLUDE_NON_ROOT_COUNTERHELPERS_H */
