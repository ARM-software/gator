/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include "ipc/message_key.h"
#include "ipc/message_traits.h"

#include <variant>

namespace ipc {
    using annotation_uid_t = int;

    /** Sent from shell->agent to tell it to shut down */
    using msg_shutdown_t = message_t<message_key_t::shutdown, void, void>;

    /** Sent from the annotation agent to the shell when a new annotation connection is received */
    using msg_annotation_new_conn_t = message_t<message_key_t::annotation_new_conn, annotation_uid_t, void>;

    /** Sent from the annotation agent to the shell when some data is received from an annotations connection */
    using msg_annotation_close_conn_t = message_t<message_key_t::annotation_close_conn, annotation_uid_t, void>;

    /** Sent from the shell to the annotation agent when some data is to be sent to the annotation connection */
    using msg_annotation_recv_bytes_t =
        message_t<message_key_t::annotation_recv_bytes, annotation_uid_t, std::vector<char>>;

    /** Sent by the agent or shell to close a connection */
    using msg_annotation_send_bytes_t =
        message_t<message_key_t::annotation_send_bytes, annotation_uid_t, std::vector<char>>;

    /** All supported message types */
    using all_message_types_variant_t = std::variant<msg_shutdown_t,
                                                     msg_annotation_new_conn_t,
                                                     msg_annotation_close_conn_t,
                                                     msg_annotation_recv_bytes_t,
                                                     msg_annotation_send_bytes_t,
                                                     std::monostate>;
}
