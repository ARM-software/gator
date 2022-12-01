/* Copyright (C) 2022 by Arm Limited. All rights reserved. */
#pragma once

#include "agents/agent_worker_base.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"

#include <boost/asio/io_context.hpp>

namespace agents::perf {

    /**
     * An interface to an object that can be used to send commands to the perf capture process.
     * This allows, for example, a shell-side signal handler to request that the agent stops capturing
     * and terminates in a clean way.
     */
    class perf_capture_controller_t {
    public:
        virtual ~perf_capture_controller_t() = default;

        /**
         * Request that the perf agent process starts the capture.
         * Note that the completion handler will be called with a boolean that shows whether the
         * command was sent successfully. This does not necessarily mean the capture has actually
         * started successfully. That will be indicated by follow-up IPC messages sent from the
         * agent.
         */
        template<typename CompletionToken>
        auto async_start_capture(std::uint64_t monotonic_start, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(bool)>(
                [this, monotonic_start](auto && sc) {
                    this->async_start_capture(monotonic_start,
                                              stored_continuation_t<bool> {std::forward<decltype(sc)>(sc)});
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Request that the perf agent stop capturing. The commpletion handler will be called with
         * a boolean that shows whether the the message was sent successfully. Further IPC messages
         * will be sent from the agent as it performs cleanup & shutdown.
         */
        template<typename CompletionToken>
        auto async_stop_capture(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void()>(
                [this](auto && sc) {
                    this->async_stop_capture(stored_continuation_t<> {std::forward<decltype(sc)>(sc)});
                },
                std::forward<CompletionToken>(token));
        }

    protected:
        virtual void async_start_capture(std::uint64_t monotonic_start,
                                         async::continuations::stored_continuation_t<bool> handler_ref) = 0;

        virtual void async_stop_capture(async::continuations::stored_continuation_t<> handler_ref) = 0;
    };

    /**
     * The shell-side controller that monitors and communicates with the perf agent process.
     *
     * @tparam EventObserver The object that is to be notified of significant events from the agent
     *   process. E.g. startup/shutdown & APC frame delivery. The object is expected to conform to:
     *
     * class foo_observer {
     * public:
     *     // called once the worker has been initialised by the controller
     *     void set_controller(std::unique_ptr<perf_capture_controller_t>);
     *
     *     // called when an APC frame message is received from the agent. the data
     *     // buffer is passed to the function.
     *     void on_apc_frame_received(const std::vector<char>&);
     * };
     *
     */
    template<typename EventObserver>
    class perf_agent_worker_t : public agents::agent_worker_base_t,
                                public std::enable_shared_from_this<perf_agent_worker_t<EventObserver>> {
    public:
        static constexpr const char * get_agent_process_id() { return agent_id_perf.data(); }

        perf_agent_worker_t(boost::asio::io_context & io,
                            agent_process_t && agent_process,
                            state_change_observer_t && state_change_observer,
                            EventObserver & observer,
                            ipc::msg_capture_configuration_t capture_config)
            : agent_worker_base_t(std::move(agent_process), std::move(state_change_observer)),
              strand(io),
              observer(observer),
              capture_config(std::move(capture_config))
        {
        }

    protected:
        [[nodiscard]] boost::asio::io_context::strand & work_strand() override { return strand; }

    private:
        /**
         * An implementation of the capture controller interface that will allow the event observer
         * to send messages to the agent without having a cyclic dependency between observer & worker.
         */
        class capture_controller_t : public perf_capture_controller_t {
            using ParentType = perf_agent_worker_t<EventObserver>;

        public:
            explicit capture_controller_t(std::shared_ptr<ParentType> parent) : parent(std::move(parent)) {}

            ~capture_controller_t() override = default;

        protected:
            void async_start_capture(std::uint64_t monotonic_start,
                                     async::continuations::stored_continuation_t<bool> sc) override
            {
                parent->sink().async_send_message(
                    ipc::msg_start_t {monotonic_start},
                    [sc = std::move(sc), p = parent](const auto & ec, const auto & /*msg*/) mutable {
                        if (ec) {
                            LOG_ERROR("Error starting perf capture: %s", ec.message().c_str());
                            return resume_continuation(p->work_strand().context(), std::move(sc), false);
                        }

                        return resume_continuation(p->work_strand().context(), std::move(sc), true);
                    });
            }

            void async_stop_capture(async::continuations::stored_continuation_t<> sc) override
            {
                return submit(parent->work_strand().context(), parent->co_shutdown(), std::move(sc));
            }

        private:
            std::shared_ptr<ParentType> parent;
        };

        friend class capture_controller_t;

        boost::asio::io_context::strand strand;
        EventObserver & observer;
        ipc::msg_capture_configuration_t capture_config;

        auto co_shutdown()
        {
            using namespace async::continuations;

            return start_on(strand) //
                 | then([self = this->shared_from_this()]() -> polymorphic_continuation_t<> {
                       if (!self->transition_state(state_t::shutdown_requested)) {
                           return {};
                       }

                       LOG_DEBUG("Sending shutdown message to agent process");
                       return self->sink().async_send_message(ipc::msg_shutdown_t {}, use_continuation)
                            | then([self](const auto & /*ec*/, const auto & /*msg*/) {});
                   });
        }

        /**
         * Handle the 'ready' message - the agent has started and is waiting to be configured.
         */
        auto co_receive_message(const ipc::msg_ready_t & /*msg*/)
        {
            using namespace async::continuations;

            LOG_DEBUG("Perf agent reported that it's ready - sending config message");
            transition_state(state_t::ready);
            // send the config message to prepare the agent for the capture
            return start_on(strand) //
                 | sink().async_send_message(capture_config, use_continuation)
                 | then(
                       [self = this->shared_from_this()](const auto & ec,
                                                         const auto & /*msg*/) mutable -> polymorphic_continuation_t<> {
                           if (ec) {
                               LOG_ERROR("Failed to send the configuration to the perf agent process: %s",
                                         ec.message().c_str());
                               return self->co_shutdown();
                           }
                           return {};
                       });
        }

        /**
         * Handle the 'capture ready' message - the agent has been configured and is prepared to
         * start the capture.
         */
        auto co_receive_message(ipc::msg_capture_ready_t && msg)
        {
            LOG_DEBUG("Perf agent is prepared for capture");
            observer.on_capture_ready(std::move(msg.suffix));
        }

        /**
         * Handle the shutdown message - the agent has stopped capturing and the process is about
         * to terminate.
         */
        auto co_receive_message(const ipc::msg_shutdown_t & /*msg*/)
        {
            LOG_DEBUG("Perf agent has shut down.");
            transition_state(state_t::shutdown_received);
            return async::continuations::start_with();
        }

        auto co_receive_message(ipc::msg_apc_frame_data_t && msg)
        {
            observer.on_apc_frame_received(std::move(msg.suffix));
        }

        auto co_receive_message(ipc::msg_exec_target_app_t const & /*msg*/) { observer.exec_target_app(); }

        auto co_receive_message(ipc::msg_capture_failed_t const & msg) { observer.on_capture_failed(msg.header); }

        auto co_receive_message(ipc::msg_capture_started_t const & /*msg*/) { observer.on_capture_started(); }

    public:
        [[nodiscard]] bool start()
        {
            using namespace async::continuations;
            using namespace ipc;

            LOG_DEBUG("starting perf agent worker");
            observer.set_controller(std::make_unique<capture_controller_t>(this->shared_from_this()));

            auto self = this->shared_from_this();

            spawn("Perf shell message loop",
                  repeatedly(
                      [self]() {
                          // don't stop until the agent terminates and closes the connection from its end
                          LOG_DEBUG("Receive loop would have terminated? %d",
                                    (self->get_state() >= state_t::terminated_pending_message_loop));

                          return true;
                      },
                      [self]() {
                          return async_receive_one_of<msg_ready_t,
                                                      msg_capture_ready_t,
                                                      msg_apc_frame_data_t,
                                                      msg_shutdown_t,
                                                      msg_capture_failed_t,
                                                      msg_capture_started_t,
                                                      msg_exec_target_app_t>(self->source_shared(),
                                                                             use_continuation)
                               | map_error()           //
                               | post_on(self->strand) //
                               | unpack_variant([self](auto && msg) mutable {
                                     return self->co_receive_message(std::forward<decltype(msg)>(msg));
                                 });
                      }),
                  [self](bool failed) {
                      LOG_DEBUG("Receive loop ended");

                      boost::asio::post(self->strand, [self]() { self->set_message_loop_terminated(); });

                      if (failed) {
                          self->shutdown();
                      }
                  });

            return this->exec_agent();
        }

        void shutdown() override
        {
            using namespace async::continuations;

            LOG_DEBUG("perf worker shutdown called");

            spawn("Perf worker shutdown", co_shutdown());
        }

        void on_sigchild() override
        {
            using namespace async::continuations;

            LOG_DEBUG("perf worker: got sigchld");

            auto self = this->shared_from_this();

            spawn("sigchld handler for perf shell",
                  start_on(strand) //
                      | then([self]() {
                            self->transition_state(state_t::terminated);
                            self->observer.on_capture_completed();
                        }),
                  [self](bool failed) {
                      if (failed) {
                          self->shutdown();
                      }
                  });
        }
    };

}
