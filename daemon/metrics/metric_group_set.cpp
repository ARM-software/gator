/* Copyright (C) 2024 by Arm Limited. All rights reserved. */

#include "metrics/metric_group_set.hpp"

#include "metrics/definitions.hpp"

#include <algorithm>
#include <iterator>
#include <set>

namespace metrics {
    [[nodiscard]] bool metric_group_set_t::has_member(metric_group_id_t const item) const
    {
        if (all) {
            return true;
        }
        if (auto const & iter = members.find(item); iter == members.end()) {
            return false;
        }
        return true;
    }

    [[nodiscard]] bool metric_group_set_t::empty() const
    {
        return !all && members.empty();
    }

    [[nodiscard]] metric_group_set_t metric_group_set_t::set_union(metric_group_set_t const & rhs) const
    {
        bool const my_all = all || rhs.all;
        if (my_all) {
            return metric_group_set_t {true};
        }

        std::set<metric_group_id_t> const & lhs_members = members;
        std::set<metric_group_id_t> const & rhs_members = rhs.members;
        std::set<metric_group_id_t> unified {};
        std::set_union(lhs_members.begin(),
                       lhs_members.end(),
                       rhs_members.begin(),
                       rhs_members.end(),
                       std::inserter(unified, unified.begin()));

        return metric_group_set_t {std::move(unified)};
    }

    bool operator==(metric_group_set_t const & lhs, metric_group_set_t const & rhs)
    {
        return lhs.all == rhs.all && lhs.members == rhs.members;
    }

}
