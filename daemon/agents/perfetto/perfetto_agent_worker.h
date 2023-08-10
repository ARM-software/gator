/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */
#pragma once

#include "agents/agent_worker_base.h"
#include "agents/ext_source/ext_source_connection.h"
#include "agents/spawn_agent.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "ipc/messages.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/system/error_code.hpp>

namespace agents {

    template<typename PerfettoSource>
    class perfetto_agent_worker_t : public agent_worker_base_t,
                                    public std::enable_shared_from_this<perfetto_agent_worker_t<PerfettoSource>> {
    private:
        using weak_ptr_t = std::weak_ptr<perfetto_agent_worker_t<PerfettoSource>>;

        class connection_impl_t : public ext_source_connection_t {
        public:
            explicit connection_impl_t(weak_ptr_t agent_worker) : agent_worker(std::move(agent_worker)) {}

            ~connection_impl_t() override = default;

            void close() override
            {
                using namespace async::continuations;
                if (auto ptr = agent_worker.lock()) {
                    LOG_TRACE("Asking ext source agent to close connection");
                    auto fut = async_initiate_cont([ptr]() { return ptr->cont_shutdown(); }, boost::asio::use_future);
                    fut.get();
                }
            }

        private:
            weak_ptr_t agent_worker;
        };

        friend class connection_impl_t;

        boost::asio::io_context::strand strand;
        PerfettoSource & perfetto_source;
        std::optional<boost::asio::posix::stream_descriptor> perfetto_source_pipe {};

        /** @return A continuation that requests the remote agent to shutdown */
        auto cont_shutdown()
        {
            using namespace async::continuations;

            return start_on(strand) //
                 | then([st = this->shared_from_this()]() -> polymorphic_continuation_t<> {
                       if (!st->transition_state(state_t::shutdown_requested)) {
                           LOG_FINE("Perfetto agent worker failed to transition to the shutdown_requested state");
                           return {};
                       }

                       // tell the remote agent
                       LOG_DEBUG("Requesting perfetto agent to shut down");
                       return st->sink().async_send_message(ipc::msg_shutdown_t {}, use_continuation) //
                            | then([st](auto const & ec, auto const & /*msg*/) {
                                  if (ec) {
                                      // EOF means terminated
                                      if (ec == boost::asio::error::eof) {
                                          st->transition_state(state_t::terminated);
                                          return;
                                      }

                                      LOG_WARNING("Failed to send IPC message due to %s", ec.message().c_str());
                                  }
                              });
                   });
        }

        auto cont_on_recv_message(ipc::msg_perfetto_recv_bytes_t && msg)
        {
            using namespace async::continuations;

            const auto * data = msg.suffix.data();
            return boost::asio::async_write(*perfetto_source_pipe,                        //
                                            boost::asio::buffer(data, msg.suffix.size()), //
                                            use_continuation)                             //
                 | then([self = this->shared_from_this(),
                         msg = std::move(msg)](const auto & err, auto n) -> polymorphic_continuation_t<> {
                       if (err) {
                           LOG_ERROR("Error while forwarding perfetto source bytes: %s", err.message().c_str());
                           return self->cont_shutdown();
                       }
                       if (n != msg.suffix.size()) {
                           LOG_ERROR("Incorrect size written");
                           return self->cont_shutdown();
                       }
                       return {};
                   });
        }

        /** Handle the 'ready' IPC message variant. The agent is ready. */
        void cont_on_recv_message(ipc::msg_ready_t const & /*message*/)
        {
            LOG_FINE("Received ready message.");

            if (perfetto_source_pipe) {
                LOG_ERROR("Perfetto external data pipe already created.");
                return;
            }

            auto con = std::make_unique<connection_impl_t>(this->weak_from_this());
            auto pipe = perfetto_source.add_agent_pipe(std::move(con));

            if (!pipe) {
                LOG_ERROR("Failed to create perfetto data pipe");
                return;
            }

            perfetto_source_pipe = boost::asio::posix::stream_descriptor {strand.context(), pipe.release()};

            // transition state
            if (transition_state(state_t::ready)) {
                LOG_FINE("Perfetto agent is now ready");
            }
        }

        /** Handle the 'shutdown' IPC message variant. The agent is shutdown. */
        void cont_on_recv_message(ipc::msg_shutdown_t const & /*message*/)
        {
            LOG_FINE("Received shutdown message.");

            //close the write end.
            perfetto_source_pipe.reset();

            // transition state
            if (transition_state(state_t::shutdown_received)) {
                LOG_FINE("Perfetto agent is now shut down");
            }
        }

        auto cont_recv_message_loop()
        {
            using namespace async::continuations;
            using namespace ipc;

            auto st = this->shared_from_this();

            return repeatedly(
                [st]() {
                    // don't stop until the agent terminates and closes the connection from its end
                    LOG_DEBUG("Receive loop would have terminated? %d",
                              (st->get_state() >= state_t::terminated_pending_message_loop));
                    return true;
                },
                [st]() {
                    return async_receive_one_of<msg_ready_t, msg_shutdown_t, msg_perfetto_recv_bytes_t>(
                               st->source_shared(),
                               use_continuation)
                         | map_error()         //
                         | post_on(st->strand) //
                         | unpack_variant([st](auto && message) {
                               // NOLINTNEXTLINE(bugprone-move-forwarding-reference)
                               return st->cont_on_recv_message(std::move(message));
                           });
                });
        }

    public:
        static constexpr char const * get_agent_process_id() { return agent_id_perfetto.data(); }

        perfetto_agent_worker_t(boost::asio::io_context & io_context,
                                agent_process_t && agent_process,
                                state_change_observer_t && state_change_observer,
                                PerfettoSource & perfetto_source)
            : agent_worker_base_t(std::move(agent_process), std::move(state_change_observer)),
              strand(io_context),
              perfetto_source(perfetto_source)
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
                                LOG_FINE("perfetto agent is now terminated");
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
