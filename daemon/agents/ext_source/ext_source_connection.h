/* Copyright (C) 2022 by Arm Limited. All rights reserved. */
#pragma once

namespace agents {

    class ext_source_connection_t {
    public:
        virtual ~ext_source_connection_t() = default;
        virtual void close() = 0;
    };

}
