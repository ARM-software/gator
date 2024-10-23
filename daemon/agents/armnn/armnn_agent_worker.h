/* Copyright (C) 2023-2024 by Arm Limited. All rights reserved. */
#pragma once

#include "agents/agent_worker_base.h"
#include "agents/spawn_agent.h"
#include "armnn/ISocketIO.h"
#include "armnn/ISocketIOConsumer.h"
#include "armnn/SocketAcceptor.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "ipc/messages.h"

#include <cerrno>
#include <list>
#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>

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
     * The main gator process side of the armnn agent.
     *
     * This class maintains a record of the agent process state, and is responsible for interacting
     * with the agent process via the IPC mechanism.
     * The class will respond to msg_annotatation_read data and forward the received annotation messages
     * into the armnn driver.
     */
    class armnn_agent_worker_t : public agent_worker_base_t, public std::enable_shared_from_this<armnn_agent_worker_t> {
    private:
        using weak_ptr_t = std::weak_ptr<armnn_agent_worker_t>;

        class connection_impl_t;

        class connection_socket_io_t : public armnn::ISocketIO {
        public:
            explicit connection_socket_io_t(std::shared_ptr<connection_impl_t> connection)
                : connection(std::move(connection))
            {
            }

            void close() override { connection->close(); }

            [[nodiscard]] bool isOpen() const override { return connection->isOpen(); }

            [[nodiscard]] bool writeExact(lib::Span<const std::uint8_t> buffer) override
            {
                if (int const result = connection->send_message(buffer); result != 0) {
                    if (result < 0) {
                        errno = -result;
                        return false;
                    }
                };
                errno = 0;
                return true;
            }

            [[nodiscard]] bool readExact(lib::Span<std::uint8_t> buffer) override
            {
                if (buffer.empty()) {
                    return true;
                }

                while (!buffer.empty()) {
                    auto const n_written = connection->wait_bytes(buffer);
                    //is the connection empty and closed?
                    if (n_written == 0) {
                        return false;
                    }

                    //move the write location forward, by the number of bytes written, this will adjust the size of the remaining span, and it will be empty when all bytes are written.
                    buffer = buffer.subspan(n_written);
                }
                return true;
            }

            void interrupt() override { connection->close(); }

        private:
            std::shared_ptr<connection_impl_t> connection;
        };

        class connection_impl_t : public std::enable_shared_from_this<connection_impl_t> {
        public:
            connection_impl_t(weak_ptr_t agent_worker, ipc::annotation_uid_t id)
                : agent_worker(std::move(agent_worker)), id(id)
            {
            }

            ~connection_impl_t() = default;

            [[nodiscard]] static std::unique_ptr<armnn::ISocketIO> create_session_adapter(
                std::shared_ptr<connection_impl_t> const & connection)
            {
                return std::make_unique<connection_socket_io_t>(connection);
            }

            /** Handle the 'recv' IPC message variant. The agent received data from a connection. */
            void on_recv_bytes(std::vector<std::uint8_t> && buffer)
            {
                if (!buffer.empty()) {
                    std::unique_lock<std::mutex> lock {list_mutex};

                    list_of_received_buffers.emplace_back(std::move(buffer));

                    // unlock before notifying, as the waiting thread will take the lock
                    lock.unlock();

                    list_notifier.notify_one();
                }
            }

            [[nodiscard]] std::size_t wait_bytes(lib::Span<std::uint8_t> buffer)
            {

                runtime_assert(!buffer.empty(), "Shouldn't get an empty buffer");

                //acquire lock that prevents concurrent access from different threads
                std::unique_lock<std::mutex> lock {list_mutex};

                //loop while the connection is still active and there are no buffers recieved
                while (connection_open && list_of_received_buffers.empty()) {
                    //wait for notification from the recv function that data is available
                    list_notifier.wait(lock);
                }

                if (list_of_received_buffers.empty()) {
                    return 0;
                }

                //copy out some of the buffer
                auto & first_buffer = list_of_received_buffers.front();
                auto const n_to_take = std::min(buffer.size(), first_buffer.size());
                std::memcpy(buffer.data(), first_buffer.data(), n_to_take);

                //trim bytes of the front buffer
                if (n_to_take < first_buffer.size()) {
                    first_buffer.erase(first_buffer.begin(), first_buffer.begin() + n_to_take);
                }
                else { //or if empty, remove
                    list_of_received_buffers.pop_front();
                }

                //tell caller how many were written
                return n_to_take;
            }

            void close();

            void notify_terminated()
            {
                {
                    const std::lock_guard<std::mutex> lock {list_mutex};
                    connection_open = false;
                }
                list_notifier.notify_one();
            }

            int send_message(lib::Span<const std::uint8_t> buffer)
            {
                using namespace async::continuations;

                int result = 0;
                if (auto st = agent_worker.lock()) {
                    // close the external source pipe
                    const auto * p = static_cast<const unsigned char *>(buffer.data());
                    std::vector<std::uint8_t> charBuffer(p, p + buffer.size());

                    auto fut = async_initiate_cont(
                        [this, &result, st, charBuffer = std::move(charBuffer)]() {
                            return st->sink().async_send_message(
                                       ipc::msg_annotation_send_bytes_t {this->id, charBuffer},
                                       use_continuation)
                                 | then([&result, st](auto const & ec,
                                                      auto const & /*msg*/) -> polymorphic_continuation_t<> {
                                       if (ec) {
                                           result = ec.value();
                                           // EOF means terminated
                                           if (ec == boost::asio::error::eof) {
                                               st->transition_state(state_t::terminated);
                                               return {};
                                           }
                                           LOG_DEBUG("Failed to send IPC message due to %s", ec.message().c_str());
                                       }
                                       return {};
                                   });
                        },
                        boost::asio::use_future);
                    fut.get();
                }
                else {
                    result = -ENOSYS;
                }

                return result;
            }
            [[nodiscard]] bool isOpen()
            {
                const std::lock_guard<std::mutex> lock {list_mutex};
                return connection_open;
            }

        private:
            std::mutex list_mutex {};
            std::condition_variable list_notifier {};
            std::list<std::vector<std::uint8_t>> list_of_received_buffers {};

            weak_ptr_t agent_worker;
            ipc::annotation_uid_t id;
            bool connection_open {true};
        };

        friend class connection_impl_t;

        boost::asio::io_context::strand strand;
        armnn::ISocketIOConsumer & session_consumer;
        std::map<ipc::annotation_uid_t, std::shared_ptr<connection_impl_t>> armnn_connections {};

        /** @return A continuation that requests the remote target to shutdown */
        auto cont_shutdown()
        {
            using namespace async::continuations;

            LOG_FINE("Worker informed of shutdown, notifying armnn connections of shutdown");

            for (const auto & con : armnn_connections) {
                con.second->notify_terminated();
            }

            return start_on(strand) //
                 | then([st = this->shared_from_this()]() -> polymorphic_continuation_t<> {
                       if (!st->transition_state(state_t::shutdown_requested)) {
                           return {};
                       }

                       // tell the remote agent
                       LOG_FINE("Requesting armnn agent to shut down");
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

        // returns true if the connection by the uid was removed
        // returns false if no entry for the uid was found.
        bool remove_connection_and_notify_terminated(ipc::annotation_uid_t uid)
        {
            auto it = armnn_connections.find(uid);
            if (it == armnn_connections.end()) {
                return false;
            }

            auto con = it->second;
            if (!con) {
                armnn_connections.erase(it);
                return false;
            }

            con->notify_terminated();

            armnn_connections.erase(it);
            return true;
        }

        /** @return A continuation that closes the connection due to a write error on this end */
        auto cont_close_annotation_uid(ipc::annotation_uid_t uid)
        {
            using namespace async::continuations;

            auto st = this->shared_from_this();

            return start_on(strand) //
                 | then([st, uid]() -> polymorphic_continuation_t<> {
                       bool removed_entry = st->remove_connection_and_notify_terminated(uid);
                       if (!removed_entry) {
                           return {};
                       }

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
                LOG_FINE("armnn agent is now ready");
            }
        }

        /** Handle the 'shutdown' IPC message variant. The agent is shutdown. */
        void cont_on_recv_message(ipc::msg_shutdown_t const & /*message*/)
        {
            LOG_DEBUG("Received shutdown message.");

            // transition state
            if (transition_state(state_t::shutdown_received)) {
                LOG_DEBUG("armnn agent is now shut down");
            }
        }

        /** Handle the 'new connection' IPC message variant. The agent received a new connection. */
        void cont_on_recv_message(ipc::msg_annotation_new_conn_t const & message)
        {
            LOG_DEBUG("Received ipc::msg_annotation_new_conn_t; creating new connection %d", message.header);

            auto con = std::make_shared<connection_impl_t>(this->weak_from_this(), message.header);

            session_consumer.consumeSocket(connection_impl_t::create_session_adapter(con));
            LOG_FINE("Handed over accepted socket %d", message.header);

            auto inserted = armnn_connections.emplace(message.header, std::move(con)).second;

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
            auto it = armnn_connections.find(uid);
            if (it == armnn_connections.end()) {
                LOG_ERROR("Received data for external source but no pipe found");
                return {};
            }

            auto con = it->second;
            if (!con) {
                LOG_ERROR("Received data for external source but no pipe found");
                return {};
            }

            con->on_recv_bytes(std::move(message.suffix));
            return {};
        }

        /** Handle the 'close conn' IPC message variant. The agent closed a connection. */
        void cont_on_recv_message(ipc::msg_annotation_close_conn_t const & message)
        {
            LOG_DEBUG("Received ipc::msg_annotation_close_conn_t; uid=%d, closing fabricated socket", message.header);

            remove_connection_and_notify_terminated(message.header);
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
        static constexpr char const * get_agent_process_id() { return agent_id_armnn.data(); }

        armnn_agent_worker_t(boost::asio::io_context & io_context,
                             agent_process_t && agent_process,
                             state_change_observer_t && state_change_observer,
                             armnn::ISocketIOConsumer & session_consumer)
            : agent_worker_base_t(std::move(agent_process), std::move(state_change_observer)),
              strand(io_context),
              session_consumer(session_consumer)
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
                                LOG_DEBUG("armnn agent is now terminated");
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

    inline void armnn_agent_worker_t::connection_impl_t::close()
    {
        using namespace async::continuations;

        if (auto ptr = agent_worker.lock()) {
            LOG_TRACE("Asking armnn agent to close connection %d", id);
            auto fut =
                async_initiate_cont([ptr](ipc::annotation_uid_t id) { return ptr->cont_close_annotation_uid(id); },
                                    boost::asio::use_future,
                                    id);
            fut.get();
        }

        notify_terminated();
    }
}
