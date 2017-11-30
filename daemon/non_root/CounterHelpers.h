/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_COUNTERHELPERS_H
#define INCLUDE_NON_ROOT_COUNTERHELPERS_H

#include <type_traits>
#include <utility>

namespace non_root
{
    /**
     * Helper object to track the value of an absolute counter
     */
    template<typename T>
    class AbsoluteCounter
    {
    public:

        typedef T value_type;
        typedef typename std::conditional<std::is_integral<T>::value || std::is_floating_point<T>::value, T, const T &>::type cref_type;

        AbsoluteCounter()
                : currentValue_(),
                  changed_(true)
        {
        }

        cref_type value() const
        {
            return currentValue_;
        }

        bool changed() const
        {
            return changed_;
        }

        void done()
        {
            changed_ = false;
        }

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
    class DeltaCounter
    {
    public:

        static_assert(std::is_integral<T>::value || std::is_floating_point<T>::value, "T must be integral or float type");

        typedef T value_type;

        DeltaCounter()
                : currentValue(),
                  newValue()
        {
        }

        void update(value_type v)
        {
            newValue = v;
        }

        value_type delta() const
        {
            return newValue - currentValue;
        }

        void done()
        {
            currentValue = newValue;
        }

        bool changed() const
        {
            return newValue != currentValue;
        }

    private:

        value_type currentValue;
        value_type newValue;
    };
}

#endif /* INCLUDE_NON_ROOT_COUNTERHELPERS_H */
