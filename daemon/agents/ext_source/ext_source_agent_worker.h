/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */
#pragma once

#include "agents/agent_worker_base.h"
#include "agents/ext_source/ext_source_connection.h"
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

            return start_on(strand) //
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
                                  ExternalSource & external_source)
            : agent_worker_base_t(std::move(agent_process), std::move(state_change_observer)),
              strand(io_context),
              external_source(external_source)
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
