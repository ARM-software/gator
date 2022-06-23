/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */
#pragma once

#include "Logging.h"
#include "agents/common/socket_listener.h"
#include "agents/common/socket_reference.h"
#include "agents/common/socket_worker.h"
#include "agents/ext_source/ipc_sink_wrapper.h"
#include "async/completion_handler.h"
#include "ipc/messages.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "ipc/raw_ipc_channel_source.h"
#include "lib/Utils.h"

#include <atomic>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/system/error_code.hpp>

namespace agents {
    /**
     * The main agent object for the external data source agent
     */
    class ext_source_agent_t : public std::enable_shared_from_this<ext_source_agent_t> {
    public:
        using socket_read_worker_type = socket_read_worker_t<ipc_annotations_sink_adapter_t>;

        static constexpr std::string_view annotation_uds_parent_socket_name {"\0streamline-annotate-parent", 27};
        static constexpr std::string_view annotation_uds_data_socket_name {"\0streamline-annotate", 20};
        static constexpr std::uint16_t annotation_parent_tcp_port = 8082;
        static constexpr std::uint16_t annotation_data_tcp_port = 8083;

        static std::shared_ptr<ext_source_agent_t> create(boost::asio::io_context & io_context,
                                                          std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink,
                                                          std::shared_ptr<ipc::raw_ipc_channel_source_t> ipc_source)
        {
            return std::make_shared<ext_source_agent_t>(io_context, std::move(ipc_sink), std::move(ipc_source));
        }

        // use create... or make shared your self...
        ext_source_agent_t(boost::asio::io_context & io_context,
                           std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink,
                           std::shared_ptr<ipc::raw_ipc_channel_source_t> ipc_source)
            : io_context(io_context),
              strand(io_context),
              ipc_sink(std::move(ipc_sink)),
              ipc_source(std::move(ipc_source))
        {
        }

        /** Add a UDS annotation socket listener */
        void add_uds_annotation_listeners(std::string_view parent_name, std::string_view data_name)
        {
            // strand is used for synchronizing access to internal structures
            return boost::asio::post(strand, [st = shared_from_this(), parent_name, data_name]() {
                if (!st->is_shutdown) {
                    st->on_strand_add_agent(
                        "Annotations UDS parent listener",
                        make_uds_socket_lister([st](auto socket) { st->log_parent(std::move(socket)); },
                                               st->io_context,
                                               boost::asio::local::stream_protocol::endpoint {parent_name}));

                    st->on_strand_add_agent(
                        "Annotations UDS data listener",
                        make_uds_socket_lister([st](auto socket) { st->spawn_worker(std::move(socket)); },
                                               st->io_context,
                                               boost::asio::local::stream_protocol::endpoint {data_name}));
                }
            });
        }

        /** Add a TCP annotation socket listener */
        void add_tcp_annotation_listeners(boost::asio::ip::tcp::endpoint const & parent,
                                          boost::asio::ip::tcp::endpoint const & data)
        {
            // strand is used for synchronizing access to internal structures
            return boost::asio::post(strand, [st = shared_from_this(), parent, data]() {
                if (!st->is_shutdown) {
                    st->on_strand_add_agent(
                        "Annotations TCP parent listener",
                        make_tcp_socket_lister([st](auto socket) { st->log_parent(std::move(socket)); },
                                               st->io_context,
                                               parent));

                    st->on_strand_add_agent(
                        "Annotations TCP data listener",
                        make_tcp_socket_lister([st](auto socket) { st->spawn_worker(std::move(socket)); },
                                               st->io_context,
                                               data));
                }
            });
        }

        /** Add the default listener set */
        void add_all_defaults()
        {
            add_uds_annotation_listeners(annotation_uds_parent_socket_name, annotation_uds_data_socket_name);
            add_tcp_annotation_listeners(
                boost::asio::ip::tcp::endpoint {
                    boost::asio::ip::address_v6::loopback(),
                    annotation_parent_tcp_port,
                },
                boost::asio::ip::tcp::endpoint {
                    boost::asio::ip::address_v6::loopback(),
                    annotation_data_tcp_port,
                });
            add_tcp_annotation_listeners(
                boost::asio::ip::tcp::endpoint {
                    boost::asio::ip::address_v4::loopback(),
                    annotation_parent_tcp_port,
                },
                boost::asio::ip::tcp::endpoint {
                    boost::asio::ip::address_v4::loopback(),
                    annotation_data_tcp_port,
                });
        }

        /** Start the agent main worker loop */
        void start()
        {
            // strand is used for synchronizing access to internal structures
            return boost::asio::post(strand, [st = shared_from_this()]() { st->on_strand_do_started(); });
        }

        /** Shutdown the agent (closes all listeners and workers and then stops receiving new IPC messages) */
        void shutdown()
        {
            LOG_DEBUG("Shutdown received");

            // strand is used for synchronizing access to internal structures
            return boost::asio::post(strand, [st = shared_from_this()]() { st->on_strand_do_shutdown(); });
        }

        /** Wait for the agent to fully shut down */
        template<typename CompletionToken>
        auto async_wait_shutdown(CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void()>(
                [st = shared_from_this()](auto && handler) mutable {
                    using Handler = decltype(handler);
                    st->do_async_wait_shutdown(std::forward<Handler>(handler));
                },
                token);
        }

    private:
        static constexpr std::array<char, 1> close_parent_bytes {{0}};

        boost::asio::io_context & io_context;
        boost::asio::io_context::strand strand;
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink;
        std::shared_ptr<ipc::raw_ipc_channel_source_t> ipc_source;
        std::vector<std::shared_ptr<socket_listener_base_t>> socket_listeners {};
        std::vector<std::shared_ptr<socket_reference_base_t>> parent_connections {};
        std::vector<async::completion_handler_ref_t<>> shutdown_handlers {};
        std::map<ipc::annotation_uid_t, std::shared_ptr<socket_read_worker_type>> socket_workers {};
        ipc::annotation_uid_t uid_counter {0};
        bool is_shutdown {false};

        /** The agent is started */
        void on_strand_do_started()
        {
            // skip 'ready' if already shut down
            if (is_shutdown) {
                // now wait to receive messages
                return on_strand_do_receive_message();
            }

            // send the 'ready' IPC message
            return ipc_sink->async_send_message(
                ipc::msg_ready_t {},
                [st = shared_from_this()](auto const & ec, auto const & /*msg*/) {
                    if (ec) {
                        LOG_DEBUG("Failed to send ready IPC to host due to %s", ec.message().c_str());
                    }
                    else {
                        LOG_TRACE("Ready message sent");
                    }

                    // now wait to receive messages
                    return boost::asio::post(st->strand, [st]() { st->on_strand_do_receive_message(); });
                });
        }

        /** Handle an annotations 'parent' connection */
        template<typename Socket>
        void log_parent(Socket socket)
        {
            // strand is used for synchronizing access to internal structures
            return boost::asio::post(strand, [st = shared_from_this(), socket = std::move(socket)]() mutable {
                if (st->is_shutdown) {
                    LOG_DEBUG("Dropping new inbound connection due to shutdown");
                    return;
                }

                // store the parent connection; we don't use it for data transmission, but the annotation protocol expects the port to be maintained
                // until gatord exits
                st->parent_connections.emplace_back(make_socket_ref(std::move(socket)));
            });
        }

        /**
         * Called whenever a new connection is accepted to create a new worker from the new connection socket.
         */
        template<typename Socket>
        void spawn_worker(Socket socket)
        {
            // strand is used for synchronizing access to internal structures
            return boost::asio::post(strand, [st = shared_from_this(), socket = std::move(socket)]() mutable {
                if (st->is_shutdown) {
                    LOG_DEBUG("Dropping new inbound connection due to shutdown");
                    return;
                }

                // create it
                auto id = ++st->uid_counter;
                auto socket_read_worker =
                    socket_read_worker_type::create(ipc_annotations_sink_adapter_t(st->ipc_sink, id),
                                                    make_socket_ref(std::move(socket)));

                // store it
                st->socket_workers[id] = socket_read_worker;

                // start it
                socket_read_worker->start();
            });
        }

        /** Add one new listener to the list of socket listeners */
        template<typename ProtocolType, typename WorkerSpawnerFn>
        void on_strand_add_agent(std::string_view name,
                                 std::shared_ptr<socket_listener_t<ProtocolType, WorkerSpawnerFn>> worker)
        {
            bool socket_is_not_open = (!worker) || (!worker->is_open());
            if (socket_is_not_open) {
                if constexpr (std::is_same_v<ProtocolType, boost::asio::ip::tcp>) {
                    LOG_DEBUG("Failed to setup %s. Is the socket already in use?", name.data());
                }
                else if constexpr (std::is_same_v<ProtocolType, boost::asio::local::stream_protocol>) {
                    LOG_WARNING("Failed to setup %s. Is the socket already in use?", name.data());
                }
                else {
                    static_assert(lib::always_false<ProtocolType>::value, "Log call not implemented for socket type.");
                }
                return;
            }

            LOG_DEBUG("Added worker for %s", name.data());

            // store it
            socket_listeners.emplace_back(worker);

            // start it
            worker->start();
        }

        /** Receive the next IPC message from the IPC source */
        void on_strand_do_receive_message()
        {
            // check shutdown state
            if (is_shutdown && socket_workers.empty() && socket_listeners.empty()) {
                LOG_DEBUG("Shutdown complete. Notifying handlers.");

                // notify any handlers
                for (auto & handler : shutdown_handlers) {
                    // post each handler
                    boost::asio::post(io_context, std::move(handler));
                }

                // clear the list of handlers as we are done with them now
                shutdown_handlers.clear();
                return;
            }

            // receive next message
            ipc_source->async_recv_message(
                [st = shared_from_this()](boost::system::error_code const & ec,
                                          ipc::all_message_types_variant_t && msg_variant) mutable {
                    if (ec) {
                        LOG_DEBUG("Failed to receive IPC message due to %s", ec.message().c_str());
                        return st->shutdown();
                    }
                    // strand is used for synchronizing access to internal structures
                    return boost::asio::post(st->strand, [st, msg_variant = std::move(msg_variant)]() mutable {
                        std::visit(
                            [st](auto && message) {
                                // NOLINTNEXTLINE(bugprone-move-forwarding-reference)
                                return st->on_strand_do_handle_message(std::move(message));
                            },
                            std::move(msg_variant));
                    });
                });
        }

        /** Handle one of the IPC variant values */
        void on_strand_do_handle_message(std::monostate const & /*message*/)
        {
            LOG_DEBUG("Unexpected message std::monostate; ignoring");
            return on_strand_do_receive_message();
        }

        /** Handle one of the IPC variant values */
        void on_strand_do_handle_message(ipc::msg_annotation_new_conn_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_annotation_new_conn_t; ignoring");
            return on_strand_do_receive_message();
        }

        /** Handle one of the IPC variant values */
        void on_strand_do_handle_message(ipc::msg_annotation_recv_bytes_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_annotation_recv_bytes_t; ignoring");
            return on_strand_do_receive_message();
        }

        /** Handle one of the IPC variant values */
        void on_strand_do_handle_message(ipc::msg_capture_configuration_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_capture_ready_t; ignoring");
            return on_strand_do_receive_message();
        }

        /** Handle one of the IPC variant values */
        void on_strand_do_handle_message(ipc::msg_capture_ready_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_capture_ready_t; ignoring");
            return on_strand_do_receive_message();
        }

        /** Handle one of the IPC variant values */
        void on_strand_do_handle_message(ipc::msg_apc_frame_data_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_apc_frame_data_t; ignoring");
            return on_strand_do_receive_message();
        }

        /** Handle one of the IPC variant values */
        void on_strand_do_handle_message(ipc::msg_ready_t const & /*message*/)
        {
            LOG_DEBUG("Received ready message.");
            return on_strand_do_receive_message();
        }

        /** Handle one of the IPC variant values */
        void on_strand_do_handle_message(ipc::msg_start_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_start_t; ignoring");
            return on_strand_do_receive_message();
        }

        /** Handle one of the IPC variant values */
        void on_strand_do_handle_message(ipc::msg_exec_target_app_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_exec_target_app_t; ignoring");
            return on_strand_do_receive_message();
        }

        /** Handle one of the IPC variant values */
        void on_strand_do_handle_message(ipc::msg_cpu_state_change_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_cpu_state_change_t; ignoring");
            return on_strand_do_receive_message();
        }

        /** Handle the 'send bytes' IPC message variant. Transmit the bytes to the appropriate worker. */
        void on_strand_do_handle_message(ipc::msg_annotation_send_bytes_t message)
        {
            LOG_TRACE("Received %zu bytes for transmission to worker %d", message.suffix.size(), message.header);

            auto worker_it = socket_workers.find(message.header);
            if (worker_it == socket_workers.end()) {
                LOG_DEBUG("Received bytes for non-existant client %d", message.header);
                return on_strand_do_receive_message();
            }

            auto worker = worker_it->second;
            if (!worker) {
                LOG_DEBUG("Received bytes for non-existant client %d", message.header);
                return on_strand_do_receive_message();
            }

            return worker->async_send_bytes(
                std::move(message.suffix),
                [id = message.header, st = shared_from_this()](auto const & ec) {
                    if (ec) {
                        LOG_DEBUG("Failed to send bytes to worker %d due to %s", id, ec.message().c_str());
                        // strand is used for synchronizing access to internal structures
                        return boost::asio::post(st->strand, [id, st]() {
                            // close the failed connection
                            st->on_strand_do_close_worker_by_id(id);
                        });
                    }

                    // strand is used for synchronizing access to internal structures
                    return boost::asio::post(st->strand, [st]() { st->on_strand_do_receive_message(); });
                });
        }

        /** Handle the 'close connection' IPC message variant. Close the appropriate worker. */
        void on_strand_do_handle_message(ipc::msg_annotation_close_conn_t const & message)
        {
            on_strand_do_close_worker_by_id(message.header);
        }

        /** Handle the 'shutdown' IPC message variant. Shutdown the agent. */
        void on_strand_do_handle_message(ipc::msg_shutdown_t const & /*message*/)
        {
            LOG_DEBUG("Received shutdown message");
            return on_strand_do_shutdown();
        }

        /** Close one worker by its ID */
        void on_strand_do_close_worker_by_id(ipc::annotation_uid_t id)
        {
            auto worker_it = socket_workers.find(id);
            if (worker_it == socket_workers.end()) {
                LOG_DEBUG("Received close request for non-existant client %d", id);
                return on_strand_do_receive_message();
            }

            auto worker = worker_it->second;
            if (!worker) {
                LOG_DEBUG("Received close request for non-existant client %d", id);
                return on_strand_do_receive_message();
            }

            // remove from the map
            socket_workers.erase(worker_it);

            // close it
            return worker->async_close([st = shared_from_this()]() {
                // receive the next message
                return boost::asio::post(st->strand, [st]() { st->on_strand_do_receive_message(); });
            });
        }

        /** Do the work to shutdown the agent */
        void on_strand_do_shutdown()
        {
            // mark shutdown
            if (std::exchange(is_shutdown, true)) {
                LOG_DEBUG("Ignoring duplicate shutdown call");
                // was already shutdown. nothing to do
                return on_strand_do_receive_message();
            }

            LOG_TRACE("Closing all listeners");

            // close all listeners so their can be no new inbound connections
            for (auto & socket_listener : socket_listeners) {
                socket_listener->close();
            }

            socket_listeners.clear();

            LOG_TRACE("Closing all workers");

            // close each worker
            return on_strand_close_next_worker();
        }

        /** Close one worker (called iteratively) until such time as there are no workers left, then notify the IPC mechanism that the agent is shutdown */
        void on_strand_close_next_worker()
        {
            if (socket_workers.empty()) {
                LOG_TRACE("All workers closed. Sending shutdown message");

                // close the parent connections after writing a single 0-byte to each
                for (auto & parent : parent_connections) {
                    parent->with_socket([parent](auto & socket) {
                        boost::asio::async_write(socket,
                                                 boost::asio::buffer(close_parent_bytes),
                                                 [parent](auto const & /*ec*/, auto /*n*/) { parent->close(); });
                    });
                }
                parent_connections.clear();

                // send the 'shutdown' IPC message
                return ipc_sink->async_send_message(
                    ipc::msg_shutdown_t {},
                    [st = shared_from_this()](auto const & ec, auto const & /*msg*/) {
                        if (ec) {
                            LOG_DEBUG("Failed to send shutdown IPC to host due to %s", ec.message().c_str());
                        }
                        else {
                            LOG_TRACE("Shutdown message sent");
                        }

                        // receive the next message (will terminate if the state is fully shutdown)
                        return boost::asio::post(st->strand, [st]() { st->on_strand_do_receive_message(); });
                    });
            }

            // get the next item
            auto it = socket_workers.begin();
            auto worker = it->second;

            LOG_TRACE("Closing worker %d (%p)", it->first, worker.get());

            // remove from the map
            socket_workers.erase(it);

            // close it
            return worker->async_close([st = shared_from_this()]() {
                LOG_TRACE("Closed worker");
                // close the next worker
                return boost::asio::post(st->strand, [st]() { st->on_strand_close_next_worker(); });
            });
        }

        /** Handle the initiated async_wait_shutdown request */
        template<typename Handler>
        auto do_async_wait_shutdown(Handler handler)
        {
            // strand is used for synchronizing access to internal structures
            return boost::asio::post(strand, [st = shared_from_this(), handler = std::move(handler)]() mutable {
                // call directly if already shut down
                if (st->is_shutdown) {
                    return handler();
                }

                // store it for later
                st->shutdown_handlers.emplace_back(async::make_handler_ref(std::move(handler)));
            });
        }
    };
}
