/* Copyright (C) 2024 by Arm Limited. All rights reserved. */

#pragma once

#include "metrics/definitions.hpp"

#include <set>
#include <utility>
namespace metrics {

    /** Represents an immutable set of metric groups.
        Since the actual list of metric_group_id_t enums is expected
        to be added to, this object caters for the set of 'all'
        metric_group_id_ts specially, so that there doesn't have to be
        a hardcoded array/list/set of all metric_group_id_t somewhere in gator. */
    class metric_group_set_t {
    public:
        metric_group_set_t() = default;
        metric_group_set_t(std::set<metric_group_id_t> members) : members {std::move(members)} {}
        metric_group_set_t(bool represents_all) : all {represents_all} {}

        metric_group_set_t(metric_group_set_t const &) = default;
        metric_group_set_t(metric_group_set_t &&) = default;
        metric_group_set_t & operator=(const metric_group_set_t &) = default;
        metric_group_set_t & operator=(metric_group_set_t &&) = default;

        /**
         * @brief True if the parameter is in the set.
         *
         * @param item the metric group to test.
         * @return true if the group is in the set
         * @return false otherwise
         */
        [[nodiscard]] bool has_member(metric_group_id_t item) const;

        /**
         * @brief Compute the union of this set and the parameter.
         *
         * @param rhs the other metric_group_set_t to compute the union with.
         * @return metric_group_set_t the union of the two sets.
         */
        [[nodiscard]] metric_group_set_t set_union(metric_group_set_t const & rhs) const;

        /**
         * @brief True if metrics group set is empty.
         *
         * @return true if the group set is empty.
         * @return false otherwise
         */
        [[nodiscard]] bool empty() const;

        ~metric_group_set_t() = default;

        friend bool operator==(metric_group_set_t const & lhs, metric_group_set_t const & rhs);

    private:
        bool all {false};
        std::set<metric_group_id_t> members {};
    };

}
