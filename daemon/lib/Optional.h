/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_OPTIONAL_H
#define INCLUDE_LIB_OPTIONAL_H

#include "lib/Assert.h"

#include <utility>

namespace lib
{
    template<typename T>
    class Optional
    {
    public:

        typedef T value_type;

        Optional()
            :   wrapper(),
                hasValue(false)
        {
        }

        Optional(const value_type & t)
            :   wrapper(t),
                hasValue(true)
        {
        }

        Optional(value_type && t)
            :   wrapper(std::move(t)),
                hasValue(true)
        {
        }

        Optional(const Optional<value_type> & t)
            :   wrapper(),
                hasValue(false)
        {
            *this = t;
        }

        Optional(Optional<value_type> && t)
            :   wrapper(),
                hasValue(false)
        {
            *this = std::move(t);
        }


        ~Optional()
        {
            clear();
        }

        Optional& operator= (const value_type & t)
        {
            set(t);
            return *this;
        }

        Optional& operator= (value_type && t)
        {
            set(std::move(t));
            return *this;
        }

        Optional& operator= (const Optional<value_type> & t)
        {
            if (t) {
                set(t.get());
            }
            else {
                clear();
            }
            return *this;
        }

        Optional& operator= (Optional<value_type> && t)
        {
            if (t) {
                set(std::move(t.get()));
                t.clear();
            }
            else {
                clear();
            }
            return *this;
        }

        bool valid() const
        {
            return hasValue;
        }

        value_type & get()
        {
            runtime_assert(hasValue, "hasValue is false");

            return wrapper.data;
        }

        const value_type & get() const
        {
            runtime_assert(hasValue, "hasValue is false");

            return wrapper.data;
        }

        void set(const value_type & t)
        {
            if (hasValue) {
                // use the assignment operator
                wrapper.data = t;
            }
            else {
                // use placement new to initialize
                new (&wrapper) Wrapper(t);
                hasValue = true;
            }
        }

        void set(value_type && t)
        {
            if (hasValue) {
                // use the assignment operator
                wrapper.data = std::move(t);
            }
            else {
                // use placement new to initialize
                new (&wrapper) Wrapper(std::move(t));
                hasValue = true;
            }
        }

        void clear()
        {
            if (hasValue) {
                // manually invoke the destructor
                wrapper.data.~T();
                hasValue = false;
            }
        }

        operator bool() const
        {
            return valid();
        }

        value_type & operator * ()
        {
            return get();
        }

        const value_type & operator * () const
        {
            return get();
        }

        value_type * operator -> ()
        {
            return &get();
        }

        const value_type * operator -> () const
        {
            return &get();
        }

        bool operator == (const Optional<value_type> & that) const
        {
            if (hasValue && that.hasValue) {
                return wrapper.data == that.wrapper.data;
            }
            else {
                return hasValue == that.hasValue;
            }
        }

        bool operator != (const Optional<value_type> & that) const
        {
            if (hasValue && that.hasValue) {
                return wrapper.data != that.wrapper.data;
            }
            else {
                return hasValue != that.hasValue;
            }
        }

    private:

        /* Use a union to hold the optional value so we can manually construct/destruct and have it stack allocated */
        union Wrapper
        {
            value_type data;

            /* let Optional do the construction / destruction */
            Wrapper() {};
            Wrapper(const value_type & t) : data(t) {}
            Wrapper(value_type && t) : data(std::move(t)) {}
            ~Wrapper() {}
        };

        Wrapper wrapper;
        bool hasValue;
    };
}

#endif /* INCLUDE_LIB_OPTIONAL_H */
