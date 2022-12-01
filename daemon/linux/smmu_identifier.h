/* Copyright (C) 2022 by Arm Limited. All rights reserved. */
#pragma once

#include <array>
#include <optional>
#include <ostream>
#include <string>
#include <variant>

namespace gator::smmuv3 {

    /**
     * A type representing the value of an SMMUv3 IIDR that can be used to
     * create a wildcard IIDR from the full value.
     */
    class iidr_t {
    public:
        explicit iidr_t(std::array<std::string, 3> segments) : segments(std::move(segments)) {}

        /**
         * Check to see if this instance contains a full IIDR value or just a wildcard.
         */
        [[nodiscard]] bool has_full_iidr() const { return "_" != segments[1]; }

        /**
         * Gets the full value of the IIDR, which is the Implementor, Revision, variant
         * and ProductID fields concatenated.
         */
        [[nodiscard]] std::string get_full_value() const { return segments[0] + segments[1] + segments[2]; }

        /**
         * Gets the wildcard format of the IIDR, which is the Implementor and ProductID with the
         * Variant and Revision fields replaced with a _.
         */
        [[nodiscard]] std::string get_wildcard_value() const { return segments[0] + "_" + segments[2]; }

        friend std::ostream & operator<<(std::ostream & stream, const iidr_t & iidr)
        {
            return stream << "{ iidr = " << iidr.segments[0] << iidr.segments[1] << iidr.segments[2] << "}";
        }

        friend bool operator==(const iidr_t & lhs, const iidr_t & rhs) { return lhs.segments == rhs.segments; }

    private:
        std::array<std::string, 3> segments;
    };

    /**
     * An type that can be used to identify an SMMUv3 PMU device type, either by
     * a model name or its IIDR.
     */
    class smmuv3_identifier_t {
    public:
        enum class category_t { iidr, model_name };

        explicit smmuv3_identifier_t(std::string_view value);

        [[nodiscard]] auto get_category() const { return category; }

        [[nodiscard]] const auto & get_iidr() const { return std::get<iidr_t>(data); }

        [[nodiscard]] const auto & get_model() const { return std::get<std::string>(data); }

        friend std::ostream & operator<<(std::ostream & stream, const smmuv3_identifier_t & id)
        {
            stream << "{ category = ";
            if (id.category == category_t::iidr) {
                stream << "iidr, value = " << std::get<iidr_t>(id.data) << " }";
            }
            else {
                stream << "model, value = " << std::get<std::string>(id.data) << "}";
            }

            return stream;
        }

        friend bool operator==(const smmuv3_identifier_t & lhs, const smmuv3_identifier_t & rhs)
        {
            if (lhs.category == rhs.category) {
                if (lhs.category == category_t::iidr) {
                    return lhs.get_iidr() == rhs.get_iidr();
                }
                return lhs.get_model() == rhs.get_model();
            }
            return false;
        }

    private:
        category_t category;
        std::variant<std::string, iidr_t> data;
    };

    /**
     * Holds PMU identifier overrides that have been specified by the user via the command line
     * options. If a PMU can't be identified by any other means then gator will assume that the PMU
     * is one of these.
     */
    class default_identifiers_t {
    public:
        default_identifiers_t() = default;

        default_identifiers_t(smmuv3_identifier_t tcu_id, smmuv3_identifier_t tbu_id) : tcu_id(tcu_id), tbu_id(tbu_id)
        {
        }

        void set_tcu_identifier(smmuv3_identifier_t id) { tcu_id.emplace(std::move(id)); }

        [[nodiscard]] const std::optional<smmuv3_identifier_t> & get_tcu_identifier() const { return tcu_id; }

        void set_tbu_identifier(smmuv3_identifier_t id) { tbu_id.emplace(std::move(id)); }

        [[nodiscard]] const std::optional<smmuv3_identifier_t> & get_tbu_identifier() const { return tbu_id; }

        friend std::ostream & operator<<(std::ostream & stream, const default_identifiers_t & id)
        {
            stream << "{ tcu_identifier = ";
            if (id.tcu_id) {
                stream << id.tcu_id.value();
            }
            else {
                stream << "<null>";
            }
            stream << ", tbu_identifier = ";
            if (id.tbu_id) {
                stream << id.tbu_id.value();
            }
            else {
                stream << "<null>";
            }
            return stream << "}";
        }

        friend bool operator==(const default_identifiers_t & lhs, const default_identifiers_t & rhs)
        {
            return lhs.tbu_id == rhs.tbu_id && lhs.tcu_id == rhs.tcu_id;
        }

    private:
        std::optional<smmuv3_identifier_t> tcu_id;
        std::optional<smmuv3_identifier_t> tbu_id;
    };

}
