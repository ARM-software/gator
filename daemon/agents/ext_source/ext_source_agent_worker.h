/* Copyright (C) 2021-2025 by Arm Limited. All rights reserved. */
#pragma once

#include "Logging.h"
#include "agents/agent_worker_base.h"
#include "agents/ext_source/ext_source_connection.h"
#include "agents/spawn_agent.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "ipc/messages.h"

#include <cerrno>
#include <memory>
#include <unordered_set>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/use_future.hpp>
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
        using weak_ptr_t = std::weak_ptr<ext_source_agent_worker_t<ExternalSource>>;

        class connection_impl_t : public ext_source_connection_t {
        public:
            connection_impl_t(weak_ptr_t agent_worker, ipc::annotation_uid_t id)
                : agent_worker(std::move(agent_worker)), id(id)
            {
            }

            ~connection_impl_t() override = default;

            void close() override
            {
                using namespace async::continuations;
                if (auto ptr = agent_worker.lock()) {
                    LOG_TRACE("Asking ext source agent to close connection %d", id);
                    auto fut = async_initiate_cont([ptr](auto id) { return ptr->cont_close_annotation_uid(id); },
                                                   boost::asio::use_future,
                                                   id);
                    fut.get();
                }
            }

        private:
            weak_ptr_t agent_worker;
            ipc::annotation_uid_t id;
        };

        friend class connection_impl_t;

        boost::asio::io_context::strand strand;
        ExternalSource & external_source;
        std::map<ipc::annotation_uid_t, boost::asio::posix::stream_descriptor> external_source_pipes;

        /**
         * UIDs of external source pipes which have been closed. This helps
         * avoid errors should those UIDs be encountered in the future. This
         * will only work if UIDs are unique (which is currently the case).
         */
        std::unordered_set<ipc::annotation_uid_t> closed_external_source_pipes;
        ipc::msg_gpu_timeline_configuration_t gpu_timeline_config;

        /**
         * @brief Closes a source pipe with a given UID while managing object
         * data structures as appropriate.
         * @return Whether the source pipe was found. If false, nothing has been
         * changed.
         */
        bool close_external_source_pipe(ipc::annotation_uid_t uid)
        {
            // UIDs are eternally unique (assert rather than error as this
            // assumption is mainly to avoid misleading errors)
            assert(closed_external_source_pipes.count(uid) == 0);

            // close this end first
            auto it = external_source_pipes.find(uid);
            if (it == external_source_pipes.end()) {
                return false;
            }

            // close the write end
            it->second.close();

            // and remove from the map
            external_source_pipes.erase(it);
            closed_external_source_pipes.insert(uid);

            return true;
        }

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

        /**
         * @return A continuation that closes the connection. This is either due
         * to a write error on this end, or is simply on request (for example,
         * because Gator is closing down).
         */
        auto cont_close_annotation_uid(ipc::annotation_uid_t uid)
        {
            using namespace async::continuations;

            auto st = this->shared_from_this();

            return start_on(strand) //
                 | then([st, uid]() -> polymorphic_continuation_t<> {
                       bool source_pipe_found = st->close_external_source_pipe(uid);

                       if (!source_pipe_found) {
                           return {};
                       }

                       // close other end of the external source pipe
                       // Note that msg_annotation_close_conn_t is shared with Timeline
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

        /** Handle the 'ready' IPC message variant. The agent is ready. */
        auto cont_on_recv_message(ipc::msg_ready_t const & /*message*/)
        {
            using namespace async::continuations;

            LOG_DEBUG("Received ready message.");

            // transition state
            if (transition_state(state_t::ready)) {
                LOG_DEBUG("ext_source agent is now ready");
            }

            return start_on(strand) //
                 | sink().async_send_message(gpu_timeline_config, use_continuation)
                 | then([st = this->shared_from_this()](auto const & ec,
                                                        const auto & /*msg*/) -> polymorphic_continuation_t<> {
                       if (ec) {
                           LOG_ERROR("Failed to send the configuration to the agent process: %s", ec.message().c_str());
                           return st->cont_shutdown();
                       }
                       LOG_DEBUG("Write complete");
                       return {};
                   });
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

            auto con = std::make_unique<connection_impl_t>(this->weak_from_this(), message.header);
            auto pipe = external_source.add_agent_pipe(std::move(con));
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

        /**
         * Generic cont_on_recv_message() for when data should be sent to an APC.
         * @tparam ReceiveMessageType Must have (1) a UID header and (2) a
         * vector<uint8_t> suffix, this being the data to be written.
         * @param message Message to write to APC directory.
         */
        template<typename ReceiveMessageType>
        async::continuations::polymorphic_continuation_t<> cont_on_recv_message_send_to_apc(
            ReceiveMessageType && message)
        {
            using namespace async::continuations;

            auto uid = message.header;

            if (closed_external_source_pipes.find(uid) != closed_external_source_pipes.end()) {
                LOG_TRACE("Received data for external source with UID %d but already closed: doing nothing", uid);
                return {};
            }

            auto it = external_source_pipes.find(uid);
            if (it == external_source_pipes.end()) {
                LOG_ERROR("Received data for external source with UID %d but no pipe found", uid);
                return {};
            }

            // the buffer must be owned until it is fully sent
            auto buffer_ptr = std::make_shared<std::vector<std::uint8_t>>(std::move(message.suffix));

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

        /** Handle the 'recv' IPC message variant. The agent received data from a connection. */
        async::continuations::polymorphic_continuation_t<> cont_on_recv_message(
            ipc::msg_annotation_recv_bytes_t && message)
        {
            LOG_DEBUG("Received ipc::msg_annotation_recv_bytes_t; uid=%d, size=%zu",
                      message.header,
                      message.suffix.size());

            return cont_on_recv_message_send_to_apc(message);
        }

        /** Handle the 'close conn' IPC message variant. The agent closed a connection. */
        void cont_on_recv_message(ipc::msg_annotation_close_conn_t const & message)
        {
            LOG_DEBUG("Received ipc::msg_annotation_close_conn_t; uid=%d", message.header);
            (void) close_external_source_pipe(message.header);
        }

        /** Handle a received timeline message */
        async::continuations::polymorphic_continuation_t<> cont_on_recv_message(ipc::msg_gpu_timeline_recv_t && message)
        {
            LOG_DEBUG("Received ipc::msg_gpu_timeline_recv_t; uid=%d, size=%zu", message.header, message.suffix.size());

            return cont_on_recv_message_send_to_apc(message);
        }

        /** Handle the handshake tag (ESTATE header) for timeline */
        async::continuations::polymorphic_continuation_t<> cont_on_recv_message(
            ipc::msg_gpu_timeline_handshake_tag_t && message)
        {
            LOG_DEBUG("Received ipc::msg_gpu_timeline_handshake_tag_t (ESTATE header); uid=%d, size=%zu",
                      message.header,
                      message.suffix.size());

            return cont_on_recv_message_send_to_apc(message);
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
                    // don't stop until the agent terminates and closes the connection from its end
                    LOG_DEBUG("Receive loop would have terminated? %d",
                              (st->get_state() >= state_t::terminated_pending_message_loop));
                    return true;
                },
                [st]() {
                    return ipc::async_receive_one_of<ipc::msg_ready_t,
                                                     ipc::msg_shutdown_t,
                                                     ipc::msg_annotation_new_conn_t,
                                                     ipc::msg_annotation_recv_bytes_t,
                                                     ipc::msg_gpu_timeline_handshake_tag_t,
                                                     ipc::msg_gpu_timeline_recv_t,
                                                     ipc::msg_annotation_close_conn_t>(st->source_shared(),
                                                                                       use_continuation) //
                         | map_error()                                                                   //
                         | post_on(st->strand)                                                           //
                         | unpack_variant([st](auto && message) {
                               // NOLINTNEXTLINE(bugprone-move-forwarding-reference)
                               return st->cont_on_recv_message(std::move(message));
                           });
                });
        }

    public:
        static constexpr char const * get_agent_process_id() { return agent_id_ext_source.data(); }

        ext_source_agent_worker_t(boost::asio::io_context & io_context,
                                  agent_process_t && agent_process,
                                  state_change_observer_t && state_change_observer,
                                  ExternalSource & external_source,
                                  ipc::msg_gpu_timeline_configuration_t gpu_timeline_message)
            : agent_worker_base_t(std::move(agent_process), std::move(state_change_observer)),
              strand(io_context),
              external_source(external_source),
              gpu_timeline_config(gpu_timeline_message)

        {
        }

        /** Start the worker. Spawns the receive-message loop on the io_context */
        [[nodiscard]] bool start()
        {
            using namespace async::continuations;

            spawn("IPC message loop",
                  cont_recv_message_loop(), //
                  [st = this->shared_from_this()](bool error) {
                      LOG_DEBUG("Receive loop ended");

                      boost::asio::post(st->strand, [st]() { st->set_message_loop_terminated(); });

                      if (error) {
                          st->shutdown();
                      }
                  });

            return this->exec_agent();
        }

        /** Called when SIGCHLD is received for the remote process */
        void on_sigchild() override
        {
            using namespace async::continuations;

            spawn("SIGCHLD handler operation",
                  start_on(strand) //
                      | then([st = this->shared_from_this()]() {
                            if (st->transition_state(state_t::terminated)) {
                                LOG_DEBUG("ext_source agent is now terminated");
                            }
                        }));
        }

        /** Called to shutdown the remote process and worker */
        void shutdown() override
        {
            using namespace async::continuations;

            spawn("Shutdown request", cont_shutdown());
        }

    protected:
        [[nodiscard]] boost::asio::io_context::strand & work_strand() override { return strand; }
    };
}
