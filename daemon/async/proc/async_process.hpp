/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "async/async_line_reader.hpp"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"
#include "async/proc/process_monitor.hpp"
#include "async/proc/process_state.hpp"
#include "lib/AutoClosingFd.h"
#include "lib/error_code_or.hpp"
#include "lib/forked_process.h"

#include <deque>
#include <memory>
#include <string_view>
#include <type_traits>

#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

#include <unistd.h>

namespace async::proc {
    /**
     * Represents some process with asynchronously observable termination state
     */
    class async_process_t : public std::enable_shared_from_this<async_process_t> {
    public:
        async_process_t(process_monitor_t & process_monitor,
                        boost::asio::io_context & context,
                        process_monitor_t::fork_result_t fork_result)
            : process_monitor(process_monitor),
              strand(context),
              uid(fork_result.uid),
              process(std::move(fork_result.process))
        {
        }

        /** Start observing events; must be called once after successful configuration */
        void start();

        [[nodiscard]] bool is_terminated() const
        {
            return (!process) || (already_terminated && stdout_complete && stderr_complete);
        }

        [[nodiscard]] boost::asio::io_context & context() { return strand.context(); }
        [[nodiscard]] pid_t get_pid() const { return process.get_pid(); }

        // these may be unset if the input/output was redirected to a file / the log / discarded
        [[nodiscard]] lib::AutoClosingFd & get_stdin_write() { return process.get_stdin_write(); }
        [[nodiscard]] lib::AutoClosingFd & get_stdout_read() { return process.get_stdout_read(); }
        [[nodiscard]] lib::AutoClosingFd & get_stderr_read() { return process.get_stderr_read(); }

        /** Abort the process */
        void abort() { process.abort(); }

        /** Exec the process *iff* it has not already exec'd or aborted */
        [[nodiscard]] bool exec() { return process.exec(); }

        /** Mark stdout/stderr as completely read */
        void on_output_complete(boost::system::error_code const & ec, bool is_stderr);

        /**
         * Asynchronously wait for termination (or some error event).
         *
         * Can be rewaited multiple times after each error, once terminated (which requires both stdout and stderr to be marked as complete) only the final termination event is notified.
         */
        template<typename CompletionToken>
        auto async_wait_complete(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code, bool, int)>(
                [st = shared_from_this()](auto && stored_continuation) {
                    submit(start_on(st->strand) //
                               | then([st, sc = stored_continuation.move()]() mutable {
                                     st->do_async_wait_complete(std::move(sc));
                                 }),
                           stored_continuation.get_exceptionally());
                },
                std::forward<CompletionToken>(token));
        }

    private:
        using completion_handler_t = async::continuations::stored_continuation_t<boost::system::error_code, bool, int>;

        process_monitor_t & process_monitor;
        boost::asio::io_context::strand strand;
        process_uid_t uid;
        lib::forked_process_t process;
        completion_handler_t completion_handler {};
        std::deque<boost::system::error_code> pending_errors {};
        bool already_terminated {false};
        bool terminated_by_signal {false};
        int exit_status {0};
        bool stdout_complete {false};
        bool stderr_complete {false};

        void do_async_wait_complete(completion_handler_t sc);
        void process_event(boost::system::error_code const & ec, async::proc::process_monitor_event_t const & event);
        void notify(boost::system::error_code const & ec);
        void terminate(bool by_signal, int status);
        void flush();
    };
}
