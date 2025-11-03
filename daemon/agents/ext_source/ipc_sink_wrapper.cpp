/* Copyright (C) 2025 by Arm Limited. All rights reserved. */
#include "agents/ext_source/ipc_sink_wrapper.h"

using namespace agents;

// NOLINTNEXTLINE(cert-err58-cpp)
const std::vector<std::uint8_t> ipc_timeline_sink_adapter_t::timeline_protocol_handshake_tag =
    {'M', 'A', 'L', 'I', '_', 'G', 'P', 'U', '_', 'T', 'I', 'M', 'E', 'L', 'I', 'N', 'E', '\n'};
