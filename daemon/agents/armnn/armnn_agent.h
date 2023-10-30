/* Copyright (C) 2023 by Arm Limited. All rights reserved. */
#pragma once

#include "Logging.h"
#include "agents/agent_environment.h"
#include "agents/common/socket_listener.h"
#include "agents/common/socket_reference.h"
#include "agents/common/socket_worker.h"
#include "agents/ext_source/ipc_sink_wrapper.h"
#include "async/completion_handler.h"
#include "async/continuations/continuation.h"
#include "ipc/messages.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "ipc/raw_ipc_channel_source.h"
#include "lib/Utils.h"

#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/system/error_code.hpp>

namespace agents {
    /**
     * The main agent object for the external data source agent
     */
    class armnn_agent_t : public std::enable_shared_from_this<armnn_agent_t> {
        static constexpr auto socket_path = []() {
            std::array<char, 108> path {};
            //NOLINTNEXTLINE(readability-magic-numbers)
            const std::string_view armnn_uds_socket_name {"\0gatord_namespace", 17};

            for (std::size_t i = 0; i != armnn_uds_socket_name.size(); ++i) {
                path[i] = armnn_uds_socket_name[i];
            }

            return path;
        }();

    public:
        using accepted_message_types = std::tuple<ipc::msg_annotation_send_bytes_t, ipc::msg_annotation_close_conn_t>;

        using socket_read_worker_type = socket_read_worker_t<ipc_annotations_sink_adapter_t>;

        static std::shared_ptr<armnn_agent_t> create(boost::asio::io_context & io_context,
                                                     const std::shared_ptr<ipc::raw_ipc_channel_sink_t> & ipc_sink,
                                                     const agent_environment_base_t::terminator & terminator)
        {
            return std::make_shared<armnn_agent_t>(io_context, ipc_sink, terminator);
        }

        // use create... or make shared your self...
        armnn_agent_t(boost::asio::io_context & io_context,
                      std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink,
                      [[maybe_unused]] const agent_environment_base_t::terminator & terminator)
            : io_context(io_context), strand(io_context), ipc_sink(std::move(ipc_sink))
        {
            // terminator isn't used as failed connections are closed individually, they won't kill the whole capture
        }

        /** Add the default listener set */
        void add_all_defaults()
        {
            // strand is used for synchronizing access to internal structures
            return boost::asio::post(strand, [st = shared_from_this()]() {
                if (!st->is_shutdown) {
                    st->on_strand_add_agent(
                        "ArmNN UDS data listener",
                        make_uds_socket_listener([st](auto socket) { st->spawn_worker(std::move(socket)); },
                                                 st->io_context,
                                                 uds_protocol_t::endpoint(socket_path.begin(), socket_path.end())));
                }
            });
        }

        async::continuations::polymorphic_continuation_t<> co_shutdown()
        {
            using namespace async::continuations;

            auto self = this->shared_from_this();
            return start_on(strand) | then([self]() mutable -> polymorphic_continuation_t<> {
                       if (std::exchange(self->is_shutdown, true)) {
                           return {};
                       }
                       return self->co_shutdown_workers();
                   });
        }

        async::continuations::polymorphic_continuation_t<> co_receive_message(ipc::msg_annotation_send_bytes_t msg)
        {
            return co_send_annotation_bytes(std::move(msg));
        }

        async::continuations::polymorphic_continuation_t<> co_receive_message(ipc::msg_annotation_close_conn_t msg)
        {
            return co_close_worker_by_id(msg.header);
        }

    private:
        static constexpr std::array<char, 1> close_parent_bytes {{0}};

        boost::asio::io_context & io_context;
        boost::asio::io_context::strand strand;
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink;
        std::vector<std::shared_ptr<socket_listener_base_t>> socket_listeners {};
        std::map<ipc::annotation_uid_t, std::shared_ptr<socket_read_worker_type>> socket_workers {};
        ipc::annotation_uid_t uid_counter {0};
        bool is_shutdown {false};

        /** Handle the 'send bytes' IPC message variant. Transmit the bytes to the appropriate worker. */
        async::continuations::polymorphic_continuation_t<> co_send_annotation_bytes(
            ipc::msg_annotation_send_bytes_t message)
        {
            using namespace async::continuations;

            auto self = this->shared_from_this();

            return start_on(strand)
                 | then([self, message = std::move(message)]() mutable -> polymorphic_continuation_t<> {
                       LOG_TRACE("Received %zu bytes for transmission to worker %d",
                                 message.suffix.size(),
                                 message.header);

                       auto worker_it = self->socket_workers.find(message.header);
                       if (worker_it == self->socket_workers.end()) {
                           LOG_DEBUG("Received bytes for non-existent client %d", message.header);
                           return {};
                       }

                       auto worker = worker_it->second;
                       if (!worker) {
                           LOG_DEBUG("Received bytes for non-existent client %d", message.header);
                           return {};
                       }

                       return worker->async_send_bytes(std::move(message.suffix), use_continuation)
                            | then(
                                  [id = message.header, self](const auto & ec) mutable -> polymorphic_continuation_t<> {
                                      if (ec) {
                                          LOG_DEBUG("Failed to send bytes to worker %d due to %s",
                                                    id,
                                                    ec.message().c_str());
                                          return self->co_close_worker_by_id(id);
                                      }
                                      return {};
                                  });
                   });
        }

        /** Stop listening and close all workers  */
        async::continuations::polymorphic_continuation_t<> co_shutdown_workers()
        {
            using namespace async::continuations;

            auto self = shared_from_this();
            return start_on(strand)
                 // first stop listening
                 | then([self]() mutable {
                       self->is_shutdown = true;

                       LOG_TRACE("Closing all listeners");

                       // close all listeners so there can be no new inbound connections
                       for (auto & socket_listener : self->socket_listeners) {
                           socket_listener->close();
                       }

                       self->socket_listeners.clear();

                       LOG_TRACE("Closing all workers");
                   })
                 // then close all of the workers
                 | iterate(socket_workers,
                           [self](auto it) mutable {
                               auto worker = it->second;

                               LOG_TRACE("Closing worker %d (%p)", it->first, worker.get());

                               static_assert(!std::is_const_v<decltype(worker)>);
                               return worker->async_close(use_continuation);
                           })
                 | then([self]() mutable { self->socket_workers.clear(); });
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
                    socket_read_worker_type::create(st->io_context,
                                                    ipc_annotations_sink_adapter_t(st->ipc_sink, id),
                                                    make_socket_ref(std::move(socket)));

                // store it
                st->socket_workers[id] = socket_read_worker;

                // start it
                socket_read_worker->start();
            });
        }

        /** Add one new listener to the list of socket listeners */
        template<typename WorkerSpawnerFn>
        void on_strand_add_agent(std::string_view name,
                                 std::shared_ptr<socket_listener_t<uds_protocol_t, WorkerSpawnerFn>> worker)
        {
            const bool socket_is_not_open = (!worker) || (!worker->is_open());
            if (socket_is_not_open) {
                LOG_ERROR("Failed to setup %s. Is the socket already in use?", name.data());
                return;
            }

            LOG_DEBUG("Added worker for %s", name.data());

            // store it
            socket_listeners.emplace_back(worker);

            // start it
            worker->start();
        }

        /** Close a worker given its unique ID */
        async::continuations::polymorphic_continuation_t<> co_close_worker_by_id(ipc::annotation_uid_t id)
        {
            using namespace async::continuations;
            if (is_shutdown) {
                LOG_DEBUG("Ignoring connection close request for ID [%d] since this agent is shutting down and all "
                          "connections will be closed.",
                          id);
                return {};
            }

            auto self = this->shared_from_this();

            return start_on(strand) | then([self, id]() -> async::continuations::polymorphic_continuation_t<> {
                       auto worker_it = self->socket_workers.find(id);
                       if (worker_it == self->socket_workers.end()) {
                           LOG_DEBUG("Received close request for non-existent client %d", id);
                           return {};
                       }

                       auto worker = worker_it->second;
                       if (!worker) {
                           LOG_DEBUG("Received close request for non-existent client %d", id);
                           return {};
                       }

                       // remove from the map
                       self->socket_workers.erase(worker_it);

                       // close it
                       return worker->async_close(async::continuations::use_continuation);
                   });
        }
    };
}
