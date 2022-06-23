/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "async/async_byte_reader.hpp"
#include "async/async_line_reader.hpp"
#include "async/continuations/continuation.h"
#include "async/continuations/use_continuation.h"
#include "async/proc/async_process.hpp"
#include "async/proc/process_monitor.hpp"

#include <memory>

#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

#include <sys/types.h>

namespace async::proc {

    namespace detail {
        struct discard_tag_t {
        };
        struct pipe_tag_t {
        };
        struct log_tag_t {
        };
        struct ignore_tag_t {
        };

        struct from_file_t {
            char const * filename;
        };

        struct to_file_t {
            char const * filename;
            bool truncate;
        };

        // can pass a file name as stdin
        [[nodiscard]] inline lib::error_code_or_t<lib::pipe_pair_t> create_stdin(from_file_t && ff)
        {
            return lib::pipe_pair_t::from_file(ff.filename);
        }

        // can pass a read fd as stdin
        [[nodiscard]] inline lib::error_code_or_t<lib::pipe_pair_t> create_stdin(lib::AutoClosingFd && read_from)
        {
            return lib::pipe_pair_t {std::move(read_from), {}};
        }

        // can pass an existing pipe pair as stdin; is treated like pipe_tag_t
        [[nodiscard]] inline lib::error_code_or_t<lib::pipe_pair_t> create_stdin(
            lib::error_code_or_t<lib::pipe_pair_t> && pipes)
        {
            return {std::move(pipes)};
        }

        // can pass an existing pipe pair as stdin; is treated like pipe_tag_t
        [[nodiscard]] inline lib::error_code_or_t<lib::pipe_pair_t> create_stdin(lib::pipe_pair_t && pipes)
        {
            return {std::move(pipes)};
        }

        // can discard stdin
        [[nodiscard]] inline lib::error_code_or_t<lib::pipe_pair_t> create_stdin(discard_tag_t const & /*tag*/)
        {
            return lib::pipe_pair_t::create(0);
        }

        // can create a pipe for stdin for manual processing
        [[nodiscard]] inline lib::error_code_or_t<lib::pipe_pair_t> create_stdin(pipe_tag_t const & /*tag*/)
        {
            return lib::pipe_pair_t::create(0);
        }

        // no log_tag_t for stdin as does not make sense

        // can pass a file name as stdout/stderr
        [[nodiscard]] inline lib::error_code_or_t<lib::pipe_pair_t> create_stdout_err(to_file_t && to_file)
        {
            return lib::pipe_pair_t::to_file(to_file.filename, to_file.truncate);
        }

        // can pass a read fd as stdout/stderr
        [[nodiscard]] inline lib::error_code_or_t<lib::pipe_pair_t> create_stdout_err(lib::AutoClosingFd && write_to)
        {
            return lib::pipe_pair_t {{}, std::move(write_to)};
        }

        // can pass an existing pipe pair as stdout/stderr; is treated like pipe_tag_t
        [[nodiscard]] inline lib::error_code_or_t<lib::pipe_pair_t> create_stdout_err(lib::pipe_pair_t pipes)
        {
            return {std::move(pipes)};
        }

        // discard just writes to /dev/null
        [[nodiscard]] inline lib::error_code_or_t<lib::pipe_pair_t> create_stdout_err(discard_tag_t const & /*tag*/)
        {
            return lib::pipe_pair_t::to_file("/dev/null");
        }

        // can create a pipe for stdout/stderr for manual processing
        [[nodiscard]] inline lib::error_code_or_t<lib::pipe_pair_t> create_stdout_err(pipe_tag_t const & /*tag*/)
        {
            return lib::pipe_pair_t::create(0);
        }

        // can log the output from stdout/stderr to the log
        [[nodiscard]] inline lib::error_code_or_t<lib::pipe_pair_t> create_stdout_err(log_tag_t const & /*tag*/)
        {
            return lib::pipe_pair_t::create(0);
        }

        template<typename Consumer>
        [[nodiscard]] boost::system::error_code configure_stdout_err(std::shared_ptr<async_process_t> const & process,
                                                                     Consumer && consumer,
                                                                     bool is_stderr,
                                                                     lib::AutoClosingFd & fd)
        {
            using namespace async::continuations;

            // log it
            if (fd) {
                spawn("async_process_t log loop",
                      async_consume_all_lines({process->context(), fd.release()},
                                              std::forward<Consumer>(consumer),
                                              use_continuation),
                      [is_stderr, process](bool failed, boost::system::error_code ec) {
                          if (failed) {
                              // store the failure
                              if (ec) {
                                  process->on_output_complete(ec, is_stderr);
                              }
                              else {
                                  process->on_output_complete(
                                      boost::system::errc::make_error_code(boost::system::errc::io_error),
                                      is_stderr);
                              }
                          }
                          else {
                              process->on_output_complete({}, is_stderr);
                          }
                      });
            }
            // otherwise flag as complete
            else {
                process->on_output_complete({}, is_stderr);
            }

            return {};
        }

    }

    /** A simple type tag, that wraps a consumer object and indicates that the stdout or stderr
     * should be a line consumer, where each line of output is passed to the handler as it is read */
    template<typename T>
    struct line_consumer_t {
        static_assert(std::is_invocable_v<T, pid_t, std::string_view, bool>);
        T handler;
    };

    /** A line consumer, that writes to the log */
    struct log_line_consumer_t {
        void operator()(pid_t pid, std::string_view line, bool is_stderr)
        {
            if (!line.empty()) {
                if ((line.back() == '\n') || (line.back() == '\0')) {
                    line.remove_suffix(1);
                }
                if (is_stderr) {
                    LOG_STDERR(pid, line);
                }
                else {
                    LOG_STDOUT(pid, line);
                }
            }
        }
    };

    /** A simple type tag, that wraps a consumer object and indicates that the stdout or stderr
     * should be a pipe consumer, where chunks of bytes of output is passed to the handler as it is read */
    template<typename T>
    struct pipe_consumer_t {
        static_assert(std::is_invocable_v<T, pid_t, std::string_view, bool>);
        T handler;
    };

    /** A pipe consumer, that writes to the log */
    struct log_pipe_consumer_t {
        void operator()(pid_t pid, std::string_view blob, bool is_stderr)
        {
            if (!blob.empty()) {
                if (is_stderr) {
                    LOG_STDERR(pid, blob);
                }
                else {
                    LOG_STDOUT(pid, blob);
                }
            }
        }
    };

    /** Convert a line consumer that receives lines into a line_consumer_t.handler compatible consumer */
    template<typename T>
    auto wrap_line_consumer(pid_t pid, line_consumer_t<T> && consumer, bool is_stderr)
    {
        return [pid, c = std::move(consumer.handler), is_stderr](std::string_view line) mutable {
            c(pid, line, is_stderr);
        };
    }

    /** Convert a pipe consumer that receives lines into a line_consumer_t.handler compatible consumer */
    template<typename T>
    auto wrap_pipe_consumer(pid_t pid, pipe_consumer_t<T> && consumer, bool is_stderr)
    {
        return [pid, c = std::move(consumer.handler), is_stderr](std::string_view blob) mutable {
            c(pid, blob, is_stderr);
        };
    }

    /** Used to indicate that stdin is not used by process and should be closed, or that for stdout/stderr, it should be routed to /dev/null */
    static constexpr detail::discard_tag_t discard_ioe;
    /** Used to indicate that stdin/stdout/stderr should be a pipe, which will be used externally */
    static constexpr detail::pipe_tag_t pipe_ioe;
    /** Used to indicate that stdout/stderr should be a pipe, which will be logged to */
    static constexpr detail::log_tag_t log_oe;
    /** Used to indicate that stdin should read from a file */
    [[nodiscard]] constexpr detail::from_file_t read_from(char const * filename) { return {filename}; }
    /** Used to indicate that stdout/stderr should write from a file (overwriting it) */
    [[nodiscard]] constexpr detail::to_file_t write_to(char const * filename) { return {filename, true}; }
    /** Used to indicate that stdout/stderr should write from a file (appending it) */
    [[nodiscard]] constexpr detail::to_file_t append_to(char const * filename) { return {filename, false}; }
    /** Use to extract a pipe for stdin of a new process, using stdout of the prev process */
    [[nodiscard]] lib::AutoClosingFd from_stdout(async_process_t & p);
    /** Use to extract a pipe for stdin of a new process, using stderr of the prev process */
    [[nodiscard]] lib::AutoClosingFd from_stderr(async_process_t & p);

    /**
     * Map a stdin 'mode' value to a handler type tag.
     * In this case, the discard tag is returned, indicating that stdin should be closed as it will not be used.
     */
    [[nodiscard]] constexpr detail::discard_tag_t stdin_mode_type(detail::discard_tag_t const & /*tag*/) { return {}; }

    /**
     * Map a stdin 'mode' value to a handler type tag.
     * In this case, the ignore tag is returned, indicating that stdin should not be modified as it is already closed, or will
     * be used as-is.
     */
    template<typename T>
    [[nodiscard]] constexpr detail::ignore_tag_t stdin_mode_type(T const & /*tag*/)
    {
        return {};
    }

    /**
     * Map a stdout/stderr 'mode' value to a handler type tag.
     * In this case, the log tag is mapped to a pipe consumer object, which consumes bytes from the pipe into the log
     */
    [[nodiscard]] constexpr pipe_consumer_t<log_pipe_consumer_t> stdout_err_mode_type(detail::log_tag_t const & /*tag*/)
    {
        return {};
    }

    /**
     * Map a stdout/stderr 'mode' value to a handler type tag.
     * In this case, the tag is mapped to an ignore tag meaning that stdout/stderr should not be modified as already closed, or used as-is.
     */
    template<typename T>
    [[nodiscard]] constexpr detail::ignore_tag_t stdout_err_mode_type(T const & /*tag*/)
    {
        return {};
    }

    /**
     * Configure stdin for some process.
     * In this case, because the type is the discard tag, stdin will be closed.
     */
    [[nodiscard]] boost::system::error_code configure_stdin(std::shared_ptr<async_process_t> const & /*process*/,
                                                            detail::discard_tag_t const & /*tag */,
                                                            lib::AutoClosingFd & fd);

    /**
     * Configure stdin for some process.
     * In this case, because the type is the ignore tag, nothing will happen.
     */
    [[nodiscard]] boost::system::error_code configure_stdin(std::shared_ptr<async_process_t> const & /*process*/,
                                                            detail::ignore_tag_t const & /*tag */,
                                                            lib::AutoClosingFd & /*fd*/);

    /**
     * Configure stdout/stderr for some process.
     * In this case, because the type is the ignore tag, nothing will happen, but the complete state will be update if the fd was already closed.
     */
    [[nodiscard]] boost::system::error_code configure_stdout_err(std::shared_ptr<async_process_t> const & process,
                                                                 detail::ignore_tag_t const & /*tag */,
                                                                 bool is_stderr,
                                                                 lib::AutoClosingFd & fd);

    /**
     * Configure stdout/stderr for some process.
     * In this case, because the type is the line consumer tag, the stream will be configured to asynchronously read all lines and pass each one to the consumer.
     */
    template<typename T>
    [[nodiscard]] boost::system::error_code configure_stdout_err(std::shared_ptr<async_process_t> const & process,
                                                                 line_consumer_t<T> && consumer,
                                                                 bool is_stderr,
                                                                 lib::AutoClosingFd & fd)
    {
        return detail::configure_stdout_err(process,
                                            wrap_line_consumer(process->get_pid(), std::move(consumer), is_stderr),
                                            is_stderr,
                                            fd);
    }

    /**
     * Configure stdout/stderr for some process.
     * In this case, because the type is the pipe consumer tag, the stream will be configured to asynchronously read all lines and pass each one to the consumer.
     */
    template<typename T>
    [[nodiscard]] boost::system::error_code configure_stdout_err(std::shared_ptr<async_process_t> const & process,
                                                                 pipe_consumer_t<T> && consumer,
                                                                 bool is_stderr,
                                                                 lib::AutoClosingFd & fd)
    {
        return detail::configure_stdout_err(process,
                                            wrap_pipe_consumer(process->get_pid(), std::move(consumer), is_stderr),
                                            is_stderr,
                                            fd);
    }

    /**
     * The arguments required to exec a process
     */
    struct async_exec_args_t {
        /** The process exe to run */
        std::string command;
        /** The vector of args to pass to exec */
        std::vector<std::string> args;
        /** The working directory, empty means current */
        boost::filesystem::path working_dir;
        /** Optional uid/gid pair to change to */
        std::optional<std::pair<uid_t, gid_t>> uid_gid;
        /** When true, means args[0] is not the name of the command, and command will be inserted as args[0] to exec */
        bool prepend_command;

        /** Constructor, for a command with no arguments */
        explicit async_exec_args_t(std::string command) : command(std::move(command)), prepend_command(true) {}

        /** Constructor, for a command and its arguments.
         * If args is empty, it will be set to the command, otherwise the first argument
         * must be the command name repeated as per argv[0], as per exec. */
        async_exec_args_t(std::string command, std::vector<std::string> args, bool prepend_command = true)
            : command(std::move(command)), args(std::move(args)), prepend_command(prepend_command || this->args.empty())
        {
        }

        /** Constructor, for when working directory is also required.
         * If args is empty, it will be set to the command, otherwise the first argument
         * must be the command name repeated as per argv[0], as per exec. */
        async_exec_args_t(std::string command,
                          std::vector<std::string> args,
                          boost::filesystem::path working_dir,
                          bool prepend_command = true)
            : command(std::move(command)),
              args(std::move(args)),
              working_dir(std::move(working_dir)),
              prepend_command(prepend_command || this->args.empty())
        {
        }

        /** Constructor, for all components.
         * If args is empty, it will be set to the command, otherwise the first argument
         * must be the command name repeated as per argv[0], as per exec. */
        async_exec_args_t(std::string command,
                          std::vector<std::string> args,
                          boost::filesystem::path working_dir,
                          std::optional<std::pair<uid_t, gid_t>> uid_gid,
                          bool prepend_command = true)
            : command(std::move(command)),
              args(std::move(args)),
              working_dir(std::move(working_dir)),
              uid_gid(uid_gid),
              prepend_command(prepend_command || this->args.empty())
        {
        }

        /** Constructor, for all components.
         * If args is empty, it will be set to the command, otherwise the first argument
         * must be the command name repeated as per argv[0], as per exec. */
        async_exec_args_t(std::string command,
                          std::vector<std::string> args,
                          boost::filesystem::path working_dir,
                          uid_t uid,
                          gid_t gid,
                          bool prepend_command = true)
            : command(std::move(command)),
              args(std::move(args)),
              working_dir(std::move(working_dir)),
              uid_gid({uid, gid}),
              prepend_command(prepend_command || this->args.empty())
        {
        }
    };

    /**
     * Create an async_process_t. The process will be created in an unconfigured state.
     * The completion handler must configure, then start, then exec the process.
     */
    template<typename CompletionToken>
    static auto async_create_process(process_monitor_t & process_monitor,
                                     async_exec_args_t exec_args,
                                     lib::stdio_fds_t stdio_fds,
                                     CompletionToken && token)
    {
        using namespace async::continuations;

        LOG_DEBUG("Creating process %s", exec_args.command.c_str());

        return async_initiate(
            [&process_monitor, exec_args = std::move(exec_args), stdio_fds = std::move(stdio_fds)]() mutable {
                return process_monitor.async_fork_exec(exec_args.prepend_command,
                                                       std::move(exec_args.command),
                                                       std::move(exec_args.args),
                                                       std::move(exec_args.working_dir),
                                                       exec_args.uid_gid,
                                                       std::move(stdio_fds),
                                                       use_continuation);
            },
            std::forward<CompletionToken>(token));
    }

    /**
     * Create an async_process_t. The process will be created in a configured and started state.
     * The completion handler must, then exec the process.
     *
     * @param process_monitor The process monitor
     * @param context The context for which io and other processing should happen on
     * @param exec_args The set of configuration options defining the process to run
     * @param stdin_mode Indicates how stdin should be handled
     * @param stdout_mode Indicates how stdout should be handled
     * @param stderr_mode Indicates how stderr should be handled
     * @param token The async completion token
     */
    template<typename InputMode, typename OutputMode, typename ErrorMode, typename CompletionToken>
    static auto async_create_process(process_monitor_t & process_monitor,
                                     boost::asio::io_context & context,
                                     async_exec_args_t exec_args,
                                     InputMode && stdin_mode,
                                     OutputMode && stdout_mode,
                                     ErrorMode && stderr_mode,
                                     CompletionToken && token)
    {
        using namespace async::continuations;

        return async_initiate(
            [&process_monitor,
             &context,
             exec_args = std::move(exec_args),
             stdin_mode = std::forward<InputMode>(stdin_mode),
             stdout_mode = std::forward<OutputMode>(stdout_mode),
             stderr_mode = std::forward<ErrorMode>(stderr_mode)]() mutable
            -> polymorphic_continuation_t<boost::system::error_code, std::shared_ptr<async_process_t>> {
                // map to input/output mode to some handler type or tag
                auto stdin_type = stdin_mode_type(stdin_mode);
                auto stdout_type = stdout_err_mode_type(stdout_mode);
                auto stderr_type = stdout_err_mode_type(stderr_mode);

                // create the fds
                auto stdio_fds = lib::stdio_fds_t::create_from(detail::create_stdin(std::move(stdin_mode)),
                                                               detail::create_stdout_err(std::move(stdout_mode)),
                                                               detail::create_stdout_err(std::move(stderr_mode)));

                auto const * error = lib::get_error(stdio_fds);
                if (error != nullptr) {
                    LOG_DEBUG("Failed to create some io");
                    return start_with(*error, std::shared_ptr<async_process_t>());
                }

                // fork the process
                return async_create_process(process_monitor,
                                            std::move(exec_args),
                                            lib::get_value(std::move(stdio_fds)),
                                            use_continuation) //
                     | then([&process_monitor,
                             &context,
                             stdin_type = std::move(stdin_type),
                             stdout_type = std::move(stdout_type),
                             stderr_type =
                                 std::move(stderr_type)](boost::system::error_code ec,
                                                         process_monitor_t::fork_result_t fork_result) mutable {
                           LOG_DEBUG("Forked process %s, %d", ec.message().c_str(), fork_result.process.get_pid());

                           // forward error
                           if (ec) {
                               return std::pair {ec, std::shared_ptr<async_process_t> {}};
                           }

                           auto result = std::make_shared<async_process_t>(
                               async_process_t {process_monitor, context, std::move(fork_result)});

                           // configure stdin
                           ec = configure_stdin(result, std::move(stdin_type), result->get_stdin_write());
                           if (ec) {
                               return std::pair {ec, std::shared_ptr<async_process_t> {}};
                           }

                           // configure stdout
                           ec = configure_stdout_err(result, std::move(stdout_type), false, result->get_stdout_read());
                           if (ec) {
                               return std::pair {ec, std::shared_ptr<async_process_t> {}};
                           }

                           // configure stderr
                           ec = configure_stdout_err(result, std::move(stderr_type), true, result->get_stderr_read());
                           if (ec) {
                               return std::pair {ec, std::shared_ptr<async_process_t> {}};
                           }

                           // start observing events
                           result->start();

                           return std::pair {ec, result};
                       }) //
                     | unpack_tuple();
            },
            std::forward<CompletionToken>(token));
    }

    /** Helper that takes async_process_t and waits for it to complete. */
    template<typename CompletionToken>
    auto async_wait_for_completion(std::shared_ptr<async_process_t> const & process, CompletionToken && token)
    {
        using namespace async::continuations;

        return async_initiate<continuation_of_t<boost::system::error_code, bool, int>>(
            [process]() mutable -> polymorphic_continuation_t<boost::system::error_code, bool, int> {
                // read off events until it terminates
                return repeatedly([process]() { return !process->is_terminated(); },
                                  [process]() {
                                      LOG_DEBUG("Waiting for event %d", process->get_pid());

                                      return process->async_wait_complete(use_continuation) //
                                           | then([process](auto ec, auto by_signal, auto status) {
                                                 if (ec) {
                                                     LOG_DEBUG("unexpected error reported for process %d (%s)",
                                                               process->get_pid(),
                                                               ec.message().c_str());
                                                 }
                                                 else {
                                                     LOG_DEBUG("process %d terminated due to %s with status=%d",
                                                               process->get_pid(),
                                                               (by_signal ? "signal" : "exit"),
                                                               status);
                                                 }
                                             });
                                  })
                     // reading one last time just gets the final exit state
                     | process->async_wait_complete(use_continuation);
            },
            std::forward<CompletionToken>(token));
    }

    /** Helper that takes async_process_t and runs it to completion. */
    template<typename CompletionToken>
    auto async_run_to_completion(std::shared_ptr<async_process_t> const & process, CompletionToken && token)
    {
        using namespace async::continuations;

        return async_initiate<continuation_of_t<boost::system::error_code, bool, int>>(
            [process]() mutable -> polymorphic_continuation_t<boost::system::error_code, bool, int> {
                // exec the process
                if (!process->exec()) {
                    LOG_DEBUG("Exec failed for %d", process->get_pid());

                    return start_with(boost::system::errc::make_error_code(boost::system::errc::no_such_process),
                                      false,
                                      0);
                }
                // wait for it to finish
                return async_wait_for_completion(process, use_continuation);
            },
            std::forward<CompletionToken>(token));
    }

    /** Helper that takes a continuation returned by `async_process_t::async_create_process(..., use_continuation)` and runs it to completion. */
    template<typename StateChain, typename CompletionToken>
    auto async_run_to_completion(
        async::continuations::continuation_t<StateChain, boost::system::error_code, std::shared_ptr<async_process_t>> &&
            continuation,
        CompletionToken && token)
    {
        using namespace async::continuations;

        return async_initiate<continuation_of_t<boost::system::error_code, bool, int>>(
            [c = std::move(continuation)]() mutable {
                return std::move(c) //
                     | map_error()  //
                     | then([](std::shared_ptr<async_process_t> const & ap)
                                -> polymorphic_continuation_t<boost::system::error_code, bool, int> {
                           LOG_DEBUG("Successfully started process %d", ap->get_pid());
                           return async_run_to_completion(ap, use_continuation);
                       });
            },
            std::forward<CompletionToken>(token));
    }

    /**
     * Run a process to completion and asynchronously wait for that completion
     *
     * @param process_monitor The process monitor
     * @param context The context for which io and other processing should happen on
     * @param exec_args The exec configuration
     * @param stdin_mode Indicates how stdin should be handled
     * @param stdout_mode Indicates how stdout should be handled
     * @param stderr_mode Indicates how stderr should be handled
     * @param token The async completion token
     */
    template<typename InputMode, typename OutputMode, typename ErrorMode, typename CompletionToken>
    inline auto async_exec(process_monitor_t & process_monitor,
                           boost::asio::io_context & context,
                           async_exec_args_t exec_args,
                           InputMode && stdin_mode,
                           OutputMode && stdout_mode,
                           ErrorMode && stderr_mode,
                           CompletionToken && token)
    {
        using namespace async::continuations;

        return async_run_to_completion(async_create_process(process_monitor,
                                                            context,
                                                            std::move(exec_args),
                                                            std::forward<InputMode>(stdin_mode),
                                                            std::forward<OutputMode>(stdout_mode),
                                                            std::forward<ErrorMode>(stderr_mode),
                                                            use_continuation),
                                       std::forward<CompletionToken>(token));
    }

    /**
     * Run a process to completion and asynchronously wait for that completion.
     *
     * Any output is written to the log.
     *
     * @param process_monitor The process monitor
     * @param context The context for which io and other processing should happen on
     * @param exec_args The exec configuration
     * @param token The async completion token
     */
    template<typename CompletionToken>
    inline auto async_exec(process_monitor_t & process_monitor,
                           boost::asio::io_context & context,
                           async_exec_args_t exec_args,
                           CompletionToken && token)
    {
        using namespace async::continuations;

        return async_exec(process_monitor,
                          context,
                          std::move(exec_args),
                          discard_ioe,
                          log_oe,
                          log_oe,
                          std::forward<CompletionToken>(token));
    }

    /**
     * Run a process to completion and asynchronously wait for that completion.
     *
     * Allows configuring the stdout/stderr (e.g. for redirection to a file)
     *
     * @param process_monitor The process monitor
     * @param context The context for which io and other processing should happen on
     * @param exec_args The exec configuration
     * @param stdout_mode Indicates how stdout should be handled
     * @param stderr_mode Indicates how stderr should be handled
     * @param token The async completion token
     */
    template<typename OutputMode, typename ErrorMode, typename CompletionToken>
    inline auto async_exec(process_monitor_t & process_monitor,
                           boost::asio::io_context & context,
                           async_exec_args_t exec_args,
                           OutputMode && stdout_mode,
                           ErrorMode && stderr_mode,
                           CompletionToken && token)
    {
        using namespace async::continuations;

        return async_run_to_completion(async_create_process(process_monitor,
                                                            context,
                                                            std::move(exec_args),
                                                            discard_ioe,
                                                            std::forward<OutputMode>(stdout_mode),
                                                            std::forward<ErrorMode>(stderr_mode),
                                                            use_continuation),
                                       std::forward<CompletionToken>(token));
    }

    /**
     * Run a process to completion and asynchronously wait for that completion.
     *
     * Allows configuring the stdout/stderr (e.g. for redirection to a file)
     *
     * @param process_monitor The process monitor
     * @param context The context for which io and other processing should happen on
     * @param from_process The process to pipe from
     * @param use_stderr True to pipe from stderr, false to pipe from stdout (the previous process must have been correctly setup with pipe on that fd)
     * @param exec_args The exec configuration
     * @param stdout_mode Indicates how stdout should be handled
     * @param stderr_mode Indicates how stderr should be handled
     * @param token The async completion token
     */
    template<typename OutputMode, typename ErrorMode, typename CompletionToken>
    inline auto async_exec_piped(process_monitor_t & process_monitor,
                                 boost::asio::io_context & context,
                                 std::shared_ptr<async_process_t> const & from_process,
                                 bool use_stderr,
                                 async_exec_args_t exec_args,
                                 OutputMode && stdout_mode,
                                 ErrorMode && stderr_mode,
                                 CompletionToken && token)
    {
        using namespace async::continuations;

        return async_run_to_completion(
            start_by([from_process]() {
                return (from_process->exec()
                            ? boost::system::error_code {}
                            : boost::system::errc::make_error_code(boost::system::errc::no_such_process));
            })                //
                | map_error() //
                | async_create_process(
                    process_monitor,
                    context,
                    std::move(exec_args),
                    (use_stderr ? async::proc::from_stderr(*from_process) : async::proc::from_stdout(*from_process)),
                    std::forward<OutputMode>(stdout_mode),
                    std::forward<ErrorMode>(stderr_mode),
                    use_continuation),
            std::forward<CompletionToken>(token));
    }
}
