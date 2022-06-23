/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#include "agents/perf/perf_buffer_consumer.h"

#include "GatorException.h"

#include <limits>

namespace agents::perf {

    namespace detail {

        void validate(const buffer_config_t & config)
        {
            if (((config.page_size - 1) & config.page_size) != 0) {
                LOG_ERROR("buffer_config_t.page_size (%zu) must be a power of 2", config.page_size);
                throw GatorException("Non power of 2 page size");
            }
            if (((config.data_buffer_size - 1) & config.data_buffer_size) != 0) {
                LOG_ERROR("buffer_config_t.data_buffer_size (%zu) must be a power of 2", config.data_buffer_size);
                throw GatorException("Non power of 2 data buffer size");
            }
            if (config.data_buffer_size < config.page_size) {
                LOG_ERROR(
                    "buffer_config_t.data_buffer_size (%zu) must be a multiple of buffer_config_t.page_size (%zu)",
                    config.data_buffer_size,
                    config.page_size);
                throw GatorException("Data buffer must be a multiple of page size");
            }

            if (((config.aux_buffer_size - 1) & config.aux_buffer_size) != 0) {
                LOG_ERROR("buffer_config_t.aux_buffer_size (%zu) must be a power of 2", config.aux_buffer_size);
                throw GatorException("Aux buffer size must be a power of 2");
            }
            if ((config.aux_buffer_size < config.page_size) && (config.aux_buffer_size != 0)) {
                LOG_ERROR("buffer_config_t.aux_buffer_size (%zu) must be a multiple of buffer_config_t.page_size (%zu)",
                          config.aux_buffer_size,
                          config.page_size);
                throw GatorException("Aux buffer must be a multiple of page size");
            }
        }
    }

    perf_buffer_consumer_t::perf_buffer_consumer_t(boost::asio::io_context & io, buffer_config_t config)
        : strand(io),
          config(config),
          data_encoder {std::make_shared<data_encoder_type>(strand)},
          aux_encoder {std::make_shared<aux_encoder_type>(strand)}
    {
        detail::validate(config);
    }

}
