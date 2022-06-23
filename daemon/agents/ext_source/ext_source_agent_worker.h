/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */
#pragma once

#include "agents/agent_worker_base.h"
#include "agents/spawn_agent.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "ipc/messages.h"

#include <cerrno>
#include <memory>
#include <stdexcept>
#include <variant>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/system/error_code.hpp>

namespace agents {
    /**
     * The main gator process side of the ext_source agent.
     *
     * This class maintains a record of the agent process state, and is responsible for interacting
     * with the agent process via the IPC mechanism.
     * The class will respond to msg_annoatation_read data and forward the received annotation messages
     * into the ExternalSource class for insertion into the APC data.
     */
    template<typename ExternalSource>
    class ext_source_agent_worker_t : public agent_worker_base_t,
                                      public std::enable_shared_from_this<ext_source_agent_worker_t<ExternalSource>> {
    private:
        boost::asio::io_context::strand strand;
        ExternalSource & external_source;
        std::map<ipc::annotation_uid_t, boost::asio::posix::stream_descriptor> external_source_pipes {};

        /** @return A continuation that requests the remote target to shutdown */
        auto cont_shutdown()
        {
            using namespace async::continuations;

            return start_on(strand) //
                 | then([st = this->shared_from_this()]() -> polymorphic_continuation_t<> {
                       if (!st->transition_state(state_t::shutdown_requested)) {
                           return {};
                       }

                       // tell the remote agent
                       LOG_DEBUG("Requesting ext_source agent to shut down");
                       return st->sink().async_send_message(ipc::msg_shutdown_t {}, use_continuation) //
                            | then([st](auto const & ec, auto const & /*msg*/) {
                                  if (ec) {
                                      // EOF means terminated
                                      if (ec == boost::asio::error::eof) {
                                          st->transition_state(state_t::terminated);
                                          return;
                                      }

                                      LOG_DEBUG("Failed to send IPC message due to %s", ec.message().c_str());
                                  }
                              });
                   });
        }

        /** @return A continuation that closes the connection due to a write error on this end */
        auto cont_close_annotation_uid(ipc::annotation_uid_t uid)
        {
            using namespace async::continuations;

            auto st = this->shared_from_this();

            return start_on<on_executor_mode_t::dispatch>(strand) //
                 | then([st, uid]() -> polymorphic_continuation_t<> {
                       // close this end first
                       auto it = st->external_source_pipes.find(uid);
                       if (it == st->external_source_pipes.end()) {
                           return {};
                       }

                       // close the write end
                       it->second.close();

                       // and remove from the map
                       st->external_source_pipes.erase(it);

                       // close the external source pipe
                       return st->sink().async_send_message(ipc::msg_annotation_close_conn_t {uid}, use_continuation)
                            | then([st](auto const & ec, auto const & /*msg*/) -> polymorphic_continuation_t<> {
                                  if (ec) {
                                      // EOF means terminated
                                      if (ec == boost::asio::error::eof) {
                                          st->transition_state(state_t::terminated);
                                          return {};
                                      }

                                      LOG_DEBUG("Failed to receive IPC message due to %s", ec.message().c_str());
                                      return st->cont_shutdown();
                                  }
                                  return {};
                              });
                   });
        }

        /** Handle one of the IPC variant values */
        static void cont_on_recv_message(std::monostate const & /*message*/)
        {
            LOG_DEBUG("Unexpected message std::monostate; ignoring");
        }

        /** Handle one of the IPC variant values */
        static void cont_on_recv_message(ipc::msg_capture_configuration_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_capture_ready_t; ignoring");
        }

        /** Handle one of the IPC variant values */
        static void cont_on_recv_message(ipc::msg_capture_ready_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_capture_ready_t; ignoring");
        }

        /** Handle one of the IPC variant values */
        static void cont_on_recv_message(ipc::msg_apc_frame_data_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_apc_frame_data_t; ignoring");
        }

        /** Handle one of the IPC variant values */
        static void cont_on_recv_message(ipc::msg_start_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_start_t; ignoring");
        }

        /** Handle one of the IPC variant values */
        static void cont_on_recv_message(ipc::msg_exec_target_app_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_exec_target_app_t; ignoring");
        }

        /** Handle one of the IPC variant values */
        static void cont_on_recv_message(ipc::msg_cpu_state_change_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_cpu_state_change_t; ignoring");
        }

        /** Handle the 'ready' IPC message variant. The agent is ready. */
        void cont_on_recv_message(ipc::msg_ready_t const & /*message*/)
        {
            LOG_DEBUG("Received ready message.");

            // transition state
            if (transition_state(state_t::ready)) {
                LOG_DEBUG("ext_source agent is now ready");
            }
        }

        /** Handle the 'shutdown' IPC message variant. The agent is shutdown. */
        void cont_on_recv_message(ipc::msg_shutdown_t const & /*message*/)
        {
            LOG_DEBUG("Received shutdown message.");

            // transition state
            if (transition_state(state_t::shutdown_received)) {
                LOG_DEBUG("ext_source agent is now shut down");
            }
        }

        /** Handle the 'new connection' IPC message variant. The agent received a new connection. */
        void cont_on_recv_message(ipc::msg_annotation_new_conn_t const & message)
        {
            LOG_DEBUG("Received ipc::msg_annotation_new_conn_t; creating new connection %d", message.header);

            auto pipe = external_source.add_agent_pipe();
            if (!pipe) {
                LOG_ERROR("Failed to create external data pipe");
                return;
            }

            auto [it, inserted] =
                external_source_pipes.emplace(message.header,
                                              boost::asio::posix::stream_descriptor {strand.context(), pipe.release()});

            (void) it;

            if (!inserted) {
                LOG_ERROR("Failed to create external data pipe, does the UID already exist?");
                return;
            }
        }

        /** Handle the 'recv' IPC message variant. The agent received data from a connection. */
        async::continuations::polymorphic_continuation_t<> cont_on_recv_message(
            ipc::msg_annotation_recv_bytes_t && message)
        {
            using namespace async::continuations;

            LOG_DEBUG("Received ipc::msg_annotation_recv_bytes_t; uid=%d, size=%zu",
                      message.header,
                      message.suffix.size());

            auto uid = message.header;
            auto it = external_source_pipes.find(uid);
            if (it == external_source_pipes.end()) {
                LOG_ERROR("Received data for external source but no pipe found");
                return {};
            }

            // the buffer must be owned until it is fully sent
            auto buffer_ptr = std::make_shared<std::vector<char>>(std::move(message.suffix));

            LOG_DEBUG("Writing received data into APC");

            return boost::asio::async_write(it->second,
                                            boost::asio::buffer(*buffer_ptr),
                                            use_continuation) //
                 | then([buffer_ptr, uid, st = this->shared_from_this()](auto const & ec,
                                                                         auto n) -> polymorphic_continuation_t<> {
                       if (ec) {
                           LOG_ERROR("Forwarding external bytes failed due to %s", ec.message().c_str());
                           return st->cont_close_annotation_uid(uid);
                       }
                       if (n != buffer_ptr->size()) {
                           LOG_ERROR("Incorrect size written");
                           return st->cont_close_annotation_uid(uid);
                       }
                       LOG_DEBUG("Write complete");
                       return {};
                   });
        }

        /** Handle the 'send' IPC message variant. The agent received data from a connection. */
        static void cont_on_recv_message(ipc::msg_annotation_send_bytes_t const & /*message*/)
        {
            LOG_DEBUG("Unexpected message ipc::msg_annotation_send_bytes_t; ignoring");
        }

        /** Handle the 'close conn' IPC message variant. The agent closed a connection. */
        void cont_on_recv_message(ipc::msg_annotation_close_conn_t const & message)
        {
            LOG_DEBUG("Received ipc::msg_annotation_close_conn_t; uid=%d", message.header);

            auto it = external_source_pipes.find(message.header);
            if (it == external_source_pipes.end()) {
                return;
            }

            // close the write end
            it->second.close();

            // and remove from the map
            external_source_pipes.erase(it);
        }

        /**
         * @return A continuation that performs the receive-message loop
         */
        auto cont_recv_message_loop()
        {
            using namespace async::continuations;

            auto st = this->shared_from_this();

            return repeatedly(
                [st]() {
                    return start_on(st->strand) //
                         | then([st]() { return (st->get_state() != state_t::terminated); });
                },
                [st]() {
                    return st->source().async_recv_message(use_continuation) //
                         | map_error()                                       //
                         | post_on(st->strand)                               //
                         | unpack_variant([st](auto && message) {
                               // NOLINTNEXTLINE(bugprone-move-forwarding-reference)
                               return st->cont_on_recv_message(std::move(message));
                           });
                });
        }

    public:
        static constexpr char const * get_agent_process_id() { return agent_id_ext_source.data(); }

        ext_source_agent_worker_t(boost::asio::io_context & io_context,
                                  agent_process_t const & agent_process,
                                  state_change_observer_t && state_change_observer,
                                  ExternalSource & external_source)
            : agent_worker_base_t(agent_process, std::move(state_change_observer)),
              strand(io_context),
              external_source(external_source)
        {
        }

        /** Start the worker. Spawns the receive-message loop on the io_context */
        void start()
        {
            using namespace async::continuations;

            cont_recv_message_loop() //
                | finally([st = this->shared_from_this()](auto err) {
                      // log the failure
                      if (error_swallower_t::consume("IPC message loop", err)) {
                          st->shutdown();
                      }
                  });
        }

        /** Called when SIGCHLD is received for the remote process */
        void on_sigchild() override
        {
            using namespace async::continuations;

            start_on(strand) //
                | then([st = this->shared_from_this()]() {
                      if (st->transition_state(state_t::terminated)) {
                          LOG_DEBUG("ext_source agent is now terminated");
                      }
                  }) //
                | DETACH_LOG_ERROR("SIGCHLD handler operation");
        }

        /** Called to shutdown the remote process and worker */
        void shutdown() override
        {
            using namespace async::continuations;

            cont_shutdown() //
                | DETACH_LOG_ERROR("Shutdown request");
        }
    };
}
