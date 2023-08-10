/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */
#pragma once

#include "Logging.h"
#include "Protocol.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"
#include "async/proc/process_monitor.hpp"
#include "ipc/messages.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "ipc/raw_ipc_channel_source.h"
#include "logging/agent_log.h"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/io_context.hpp>

#include <sys/prctl.h>
#include <unistd.h>

namespace agents {

    /*
     * Agent classes are expected to look like:
     *
     * class foo_agent_t {
     * public:
     *     // public typedef that tells the environment what types of IPC messages the agent
     *     // is interested in
     *     using accepted_message_types = std::tuple<message1, message2>;
     *
     *     // for each type declared in 'accepted_message_types' a public member function to handle
     *     // that message and return some type of continuation.
     *     auto co_receive_message(message_type m);
     *
     *     // a member function that performs any cleanup tasks when the environment is
     *     // shutting down. must return some type of continuation
     *     auto co_shutdown();
     * };
     *
     * Note: These methods are not threadsafe, so it is up to the caller to synchronise them (e.g. via a start_on(..)
     * continuation).
     */

    /**
     * A helper template that allows the agent_environment_t to dispatch messages to strongly-typed
     * handlers on the agent instance.
     */
    template<typename LifecycleReceiver, typename Receiver, typename... MessageTypes>
    class message_binder_t
        : public std::enable_shared_from_this<message_binder_t<LifecycleReceiver, Receiver, MessageTypes...>> {
    public:
        message_binder_t(LifecycleReceiver & lifecycle_receiver, Receiver & receiver)
            : lifecycle_receiver(lifecycle_receiver), receiver(receiver)
        {
        }

        async::continuations::polymorphic_continuation_t<> async_receive_next_message(
            std::shared_ptr<ipc::raw_ipc_channel_source_t> source)
        {
            using namespace async::continuations;

            return ipc::async_receive_one_of<ipc::msg_shutdown_t, MessageTypes...>(std::move(source),
                                                                                   use_continuation)
                 | map_error() //
                 | unpack_variant([this](auto && msg) mutable {
                       return this->co_receive_message(std::forward<decltype(msg)>(msg));
                   });
        }

    private:
        LifecycleReceiver & lifecycle_receiver;
        Receiver & receiver;

        auto co_receive_message(ipc::msg_shutdown_t msg) { return lifecycle_receiver.co_receive_message(msg); }

        template<typename MessageType>
        auto co_receive_message(MessageType && msg)
        {
            return receiver.co_receive_message(std::forward<MessageType>(msg));
        }
    };

    /**
     * A type-erased interface that allows an agent_environment_t to be manipulated in a generic way.
     */
    class agent_environment_base_t {
    public:
        /**
         * Callback type used by agents to trigger a clean shutdown in the event of a fatal error.  Typically it will
         * just call shutdown().
         */
        using terminator = std::function<void()>;

        virtual ~agent_environment_base_t() = default;

        /**
         * Returns an identifier for this agent. Can be used to set the agent's process name.
         */
        [[nodiscard]] virtual const char * name() const = 0;

        virtual void start() = 0;

        virtual void shutdown() = 0;

        /**
         * Register a callback function to be invoked when the agent tranasitions into a
         * shutdown state.
         */
        virtual void add_shutdown_handler(async::continuations::stored_continuation_t<> && handler) = 0;
    };

    /**
     * An agent environment manages the lifecycle of an agent instance. It's responsible for creating the agent
     * instance and notifying the shell once it has started, and when it eventually shuts down.
     */
    template<typename AgentType>
    class agent_environment_t : public agent_environment_base_t,
                                public std::enable_shared_from_this<agent_environment_t<AgentType>> {
    public:
        using agent_factory = std::function<std::shared_ptr<AgentType>(boost::asio::io_context &,
                                                                       async::proc::process_monitor_t & process_monitor,
                                                                       std::shared_ptr<ipc::raw_ipc_channel_sink_t>,
                                                                       terminator)>;

        static auto create(std::string instance_name,
                           boost::asio::io_context & io,
                           async::proc::process_monitor_t & process_monitor,
                           agent_factory factory,
                           std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink,
                           std::shared_ptr<ipc::raw_ipc_channel_source_t> source)
        {
            return std::make_shared<agent_environment_t<AgentType>>(std::move(instance_name),
                                                                    io,
                                                                    process_monitor,
                                                                    factory,
                                                                    std::move(sink),
                                                                    std::move(source));
        }

        /**
         * Construct an environment that will use the supplied factory function to create the agent instance.
         */
        explicit agent_environment_t(std::string instance_name,
                                     boost::asio::io_context & io,
                                     async::proc::process_monitor_t & process_monitor,
                                     agent_factory factory,
                                     std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink,
                                     std::shared_ptr<ipc::raw_ipc_channel_source_t> source)
            : instance_name(std::move(instance_name)),
              io(io),
              process_monitor(process_monitor),
              factory(factory),
              sink(std::move(sink)),
              source(std::move(source)),
              strand(io),
              is_shutdown(false)
        {
        }

        const char * name() const override { return instance_name.c_str(); }

        void start() override
        {
            boost::asio::post(strand, [self = this->shared_from_this()]() mutable { self->on_strand_start(); });
        }

        void shutdown() override
        {
            using namespace async::continuations;

            spawn("Agent shutdown",
                  start_on(strand) //
                      | then([self = this->shared_from_this()]() mutable -> polymorphic_continuation_t<> {
                            if (std::exchange(self->is_shutdown, true)) {
                                LOG_FINE("[%s] Shutdown requested by agent, but shutdown already in progress",
                                         self->instance_name.c_str());
                                return {};
                            }
                            return self->co_init_shutdown();
                        }));
        }

        void add_shutdown_handler(async::continuations::stored_continuation_t<> && handler) override
        {
            boost::asio::post(strand, [self = this->shared_from_this(), handler = std::move(handler)]() mutable {
                self->on_strand_add_shutdown_hander(std::move(handler));
            });
        }

        auto co_receive_message(ipc::msg_shutdown_t /*msg*/) { return on_shutdown_received(); }

    private:
        std::string instance_name;
        boost::asio::io_context & io;
        async::proc::process_monitor_t & process_monitor;
        agent_factory factory;
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink;
        std::shared_ptr<ipc::raw_ipc_channel_source_t> source;

        boost::asio::io_context::strand strand;

        std::vector<async::continuations::stored_continuation_t<>> shutdown_handlers {};
        bool is_shutdown;

        std::shared_ptr<AgentType> agent;

        template<typename T>
        struct message_binder_factory_t;
        template<typename... MessageTypes>
        struct message_binder_factory_t<std::tuple<MessageTypes...>> {
            static auto create_message_binder(agent_environment_t & host, AgentType & agent)
            {
                return std::make_shared<message_binder_t<agent_environment_t, AgentType, MessageTypes...>>(host, agent);
            }
        };

        void on_strand_start()
        {
            using namespace async::continuations;

            if (agent) {
                LOG_ERROR("[%s] Start message received but agent is already running", instance_name.c_str());
                return;
            }

            if (is_shutdown) {
                LOG_ERROR("[%s] Start called after environment has shut down", instance_name.c_str());
                return;
            }

            // create the agent
            auto self = this->shared_from_this();
            agent = factory(
                io,
                process_monitor,
                sink,
                // The terminator must use a weak_pointer otherwise the agent and env will contain references to each
                // other, preventing destruction
                [self_w = this->weak_from_this()]() {
                    auto self = self_w.lock();
                    if (self) {
                        self->shutdown();
                    }
                });

            // send msg_ready_t to the shell
            spawn("agent message loop",
                  start_on(strand)                                                      //
                      | sink->async_send_message(ipc::msg_ready_t {}, use_continuation) //
                      | then([self](const auto & ec, auto /*msg*/) mutable -> polymorphic_continuation_t<> {
                            if (ec) {
                                LOG_ERROR("Error sending IPC ready message: %s", ec.message().c_str());
                                return start_with();
                            }

                            return self->co_init_receive_loop();
                        }),
                  [self](bool) { self->shutdown(); });
        }

        auto co_init_receive_loop()
        {
            using namespace async::continuations;

            using message_binder_factory_type = message_binder_factory_t<typename AgentType::accepted_message_types>;
            auto binder = message_binder_factory_type::create_message_binder(*this, *agent);

            auto self = this->shared_from_this();
            return repeatedly(
                [self]() { return start_on(self->strand) | then([self]() { return !self->is_shutdown; }); },
                [self, binder]() mutable {
                    return start_on(self->strand) | binder->async_receive_next_message(self->source);
                });
        }

        async::continuations::polymorphic_continuation_t<> on_shutdown_received()
        {
            using namespace async::continuations;

            if (std::exchange(is_shutdown, true)) {
                LOG_WARNING("[%s] Shutdown message received, but shutdown already in progress", instance_name.c_str());
                return {};
            }

            LOG_FINE("[%s] Shutdown message received - scheduling shutdown continuation", instance_name.c_str());
            // ask the agent to shutdown first, then clean up the environment
            return start_on(strand) | co_init_shutdown();
        }

        /**
         * Post the shutdown message up to the shell and run the shutdown handlers.
         */
        async::continuations::polymorphic_continuation_t<> co_init_shutdown()
        {
            using namespace async::continuations;

            auto self = this->shared_from_this();
            return start_on(strand)                                        //
                 | then([self]() mutable -> polymorphic_continuation_t<> { //
                       // if the agent has been started make sure we shut it down
                       if (self->agent) {
                           return self->agent->co_shutdown();
                       }
                       return {};
                   })
                 | then([self]() mutable { //
                       return self->sink->async_send_message(ipc::msg_shutdown_t {}, use_continuation);
                   })                                                           //
                 | then([self](const auto & ec, const auto & /*msg*/) mutable { //
                       if (ec) {
                           LOG_WARNING("Failed to send shutdown IPC to host due to %s", ec.message().c_str());
                       }
                       else {
                           LOG_TRACE("[%s] Shutdown message sent", self->instance_name.c_str());
                       }
                       self->call_shutdown_handlers();
                   });
        }

        void on_strand_add_shutdown_hander(async::continuations::stored_continuation_t<> && handler)
        {
            // call the handler directly if we've already shut down
            if (is_shutdown) {
                resume_continuation(io, std::move(handler));
            }
            else {
                shutdown_handlers.emplace_back(std::move(handler));
            }
        }

        void call_shutdown_handlers()
        {
            for (auto & handler : shutdown_handlers) {
                resume_continuation(io, std::move(handler));
            }
        }
    };

    /**
     * Schedules a completion handler to be invoked when the agent shuts down.
     */
    template<typename CompletionToken>
    auto async_await_agent_shutdown(std::shared_ptr<agent_environment_base_t> agent, CompletionToken && token)
    {
        using namespace async::continuations;

        return async_initiate_explicit<void()>(
            [agent = std::move(agent)](auto && sc) { agent->add_shutdown_handler(std::forward<decltype(sc)>(sc)); },
            std::forward<CompletionToken>(token));
    }

    /**
     * A factory function that start_agent will use to create the actual agent instance.
     * This allows the common environment setup to be shared across all agents.
     */
    using environment_factory_t =
        std::function<std::shared_ptr<agent_environment_base_t>(lib::Span<const char * const>,
                                                                boost::asio::io_context &,
                                                                async::proc::process_monitor_t &,
                                                                std::shared_ptr<ipc::raw_ipc_channel_sink_t>,
                                                                std::shared_ptr<ipc::raw_ipc_channel_source_t>)>;

    /**
     * The main agent entrypoint. Sets up IPC pipes, logging, signal handlers, etc. that are
     * the same for all agent processes.
     */
    int start_agent(lib::Span<char const * const> args, const environment_factory_t & factory);
}
