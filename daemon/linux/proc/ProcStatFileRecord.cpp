/* Copyright (C) 2017-2023 by Arm Limited. All rights reserved. */

#include "linux/proc/ProcStatFileRecord.h"

#include "lib/Assert.h"
#include "lib/Format.h"

#include <cctype>
#include <optional>
#include <utility>
#include <vector>

namespace lnx {
    namespace {
        /**
         * Skip over any spaces
         *
         * @return Index into string of next non-space character
         */
        unsigned skipSpaces(const char * string, unsigned from)
        {
            while (string[from] == ' ') {
                from += 1;
            }
            return from;
        }

        /**
         * Find the next break in the stat file; either a space, newline or end of string
         *
         * @return Index into string of next space, newline or null terminator
         */
        unsigned findNextBreak(const char * string, unsigned from)
        {
            while ((string[from] != ' ') && (string[from] != '\n') && (string[from] != '\0')) {
                from += 1;
            }
            return from;
        }

        /**
         * Skip to the start of the next line, or to end of string.
         *
         * @return Index into string of start of next line, or of null terminator
         */
        unsigned skipLine(const char * string, unsigned from)
        {
            // find the end of the line/string
            while (string[from] == ' ') {
                from = findNextBreak(string, from + 1);
            }
            if (string[from] == '\0') {
                return from;
            }
            return from + 1;
        }

        /**
         * Check that some substring of "string" matches "against".
         * Specifically the characters `string[from...(from + strlen(against))]` must match and either
         * when `full` is true, `(from + strlen(against))` must be == `to`, or
         * when `full` is false, `(from + strlen(against))` must be <= `to`.
         *
         * @return True if the strings match, false otherwise
         */
        bool matchToken(const char * string,
                        const unsigned from,
                        const unsigned to,
                        const char * against,
                        const bool full)
        {
            unsigned apos = 0;
            unsigned spos = from;

            while (against[apos] != '\0') {
                if ((spos >= to) || (string[spos] != against[apos])) {
                    return false;
                }

                apos += 1;
                spos += 1;
            }

            return (!full) || (spos == to);
        }

        /**
         * Decode an unsigned long value into the argument `result`
         *
         * @return The index of the next character starting at `from` that is not a digit
         */
        unsigned decodeUnsignedLong(unsigned long & result, const char * string, const unsigned from)
        {
            result = 0;

            unsigned pos = from;
            while (std::isdigit(string[pos]) != 0) {
                result = (result * 10) + (string[pos] - '0');
                pos += 1;
            }

            return pos;
        }

        /**
         * Extract an unsigned long value from the substring of string starting at from
         *
         * @return As per skipLine
         */
        unsigned parseUnsignedLong(std::optional<unsigned long> & result, const char * string, const unsigned from)
        {
            // just decode a single value
            unsigned long value = 0;
            const unsigned pos = decodeUnsignedLong(value, string, skipSpaces(string, from));

            if (pos > from) {
                result = value;
            }
            else {
                result.reset();
            }

            return skipLine(string, pos);
        }

        /**
         * Extract a `PagingCounts` value from the substring of string starting at from. The value is encoded as two unsigned longs separated by a space.
         *
         * @return As per skipLine
         */
        unsigned parsePagingCounts(std::optional<ProcStatFileRecord::PagingCounts> & result,
                                   const char * string,
                                   const unsigned from)
        {
            // decode two values
            unsigned long in = 0;
            unsigned long out = 0;

            // decode first
            const unsigned pos_in = decodeUnsignedLong(in, string, skipSpaces(string, from));

            // decode second
            if ((pos_in > from) && (string[pos_in] == ' ')) {
                const unsigned pos_out = decodeUnsignedLong(out, string, pos_in + 1);

                if (pos_out > (pos_in + 1)) {
                    result = ProcStatFileRecord::PagingCounts(in, out);
                }
                else {
                    result.reset();
                }

                return skipLine(string, pos_out);
            }
            result.reset();

            return skipLine(string, pos_in);
        }

        /**
         * Extract the time fields for `CpuTimes`
         */
        template<unsigned N>
        static unsigned decodeCpuTimes(const char * string,
                                       const unsigned from,
                                       unsigned long long (&times)[N],
                                       unsigned & out_num_decoded)
        {
            unsigned last_pos = from;

            out_num_decoded = 0;

            for (unsigned index = 0; index < N; ++index) {
                unsigned long time_val = 0;
                const unsigned pos = decodeUnsignedLong(time_val, string, last_pos);

                if (pos <= last_pos) {
                    break;
                }

                out_num_decoded = index + 1;
                times[index] = time_val; // leave as ticks

                if (string[pos] == ' ') {
                    last_pos = pos + 1;
                }
                else {
                    return pos;
                }
            }

            return last_pos;
        }

        /**
         * Extract a `Cputimes` value from the substring of string starting at from. The value is encoded as two unsigned longs separated by a space.
         *
         * @return As per skipLine
         */
        unsigned parseCpuTime(std::vector<ProcStatFileRecord::CpuTime> & cpus,
                              const char * string,
                              unsigned identifier_from,
                              unsigned token_end)
        {
            unsigned long cpu_id;
            unsigned long long times[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

            // decode the cpu number
            if ((identifier_from + 1) < token_end) {
                const unsigned pos = decodeUnsignedLong(cpu_id, string, identifier_from);
                runtime_assert((pos + 1) == token_end,
                               lib::Format() << "Unexpected value for 'pos': " << pos << ", expected " << token_end);
            }
            else {
                cpu_id = ProcStatFileRecord::GLOBAL_CPU_TIME_ID;
            }

            // read times
            unsigned num_decoded = 0;
            const unsigned pos = decodeCpuTimes(string, skipSpaces(string, token_end), times, num_decoded);

            if (num_decoded == 10) {
                cpus.emplace_back(cpu_id, times);
            }

            return skipLine(string, pos);
        }
    }

    ProcStatFileRecord::ProcStatFileRecord(const char * stat_contents) : ProcStatFileRecord()
    {
        if (stat_contents != nullptr) {
            unsigned current_offset = 0;
            while (stat_contents[current_offset] != '\0') {

                // find the next token break
                const unsigned next_break = findNextBreak(stat_contents, current_offset);

                // if the token is terminated by anything other than a space then skip it
                if (stat_contents[next_break] != ' ') {
                    current_offset = skipLine(stat_contents, next_break);
                }
                else {
                    // use the first char to speed parsing
                    switch (stat_contents[current_offset]) {
                        // btime
                        case 'b': {
                            if (matchToken(stat_contents, current_offset + 1, next_break, "time", true)) {
                                current_offset = parseUnsignedLong(btime, stat_contents, next_break + 1);
                            }
                            else {
                                current_offset = skipLine(stat_contents, next_break);
                            }
                            break;
                        }
                        // cpu, ctxt
                        case 'c': {
                            if (matchToken(stat_contents, current_offset + 1, next_break, "pu", false)) {
                                current_offset = parseCpuTime(cpus, stat_contents, current_offset + 3, next_break + 1);
                            }
                            else if (matchToken(stat_contents, current_offset + 1, next_break, "txt", true)) {
                                current_offset = parseUnsignedLong(ctxt, stat_contents, next_break + 1);
                            }
                            else {
                                current_offset = skipLine(stat_contents, next_break);
                            }
                            break;
                        }
                        // intr
                        case 'i': {
                            if (matchToken(stat_contents, current_offset + 1, next_break, "ntr", true)) {
                                current_offset = parseUnsignedLong(intr, stat_contents, next_break + 1);
                            }
                            else {
                                current_offset = skipLine(stat_contents, next_break);
                            }
                            break;
                        }
                        // page, processes, procs_running, procs_blocked
                        case 'p': {
                            if (matchToken(stat_contents, current_offset + 1, next_break, "age", false)) {
                                current_offset = parsePagingCounts(page, stat_contents, next_break + 1);
                            }
                            else if (matchToken(stat_contents, current_offset + 1, next_break, "rocesses", true)) {
                                current_offset = parseUnsignedLong(processes, stat_contents, next_break + 1);
                            }
                            else if (matchToken(stat_contents, current_offset + 1, next_break, "rocs_running", true)) {
                                current_offset = parseUnsignedLong(procs_running, stat_contents, next_break + 1);
                            }
                            else if (matchToken(stat_contents, current_offset + 1, next_break, "rocs_blocked", true)) {
                                current_offset = parseUnsignedLong(procs_blocked, stat_contents, next_break + 1);
                            }
                            else {
                                current_offset = skipLine(stat_contents, next_break);
                            }
                            break;
                        }
                        // soft_irq, swap
                        case 's': {
                            if (matchToken(stat_contents, current_offset + 1, next_break, "wap", false)) {
                                current_offset = parsePagingCounts(swap, stat_contents, next_break + 1);
                            }
                            else if (matchToken(stat_contents, current_offset + 1, next_break, "oftirq", true)) {
                                current_offset = parseUnsignedLong(soft_irq, stat_contents, next_break + 1);
                            }
                            else {
                                current_offset = skipLine(stat_contents, next_break);
                            }
                            break;
                        }
                        // all others
                        default: {
                            current_offset = skipLine(stat_contents, next_break);
                            break;
                        }
                    }
                }
            }
        }
    }

    ProcStatFileRecord::ProcStatFileRecord(std::vector<CpuTime> && cpus_,
                                           std::optional<PagingCounts> && page_,
                                           std::optional<PagingCounts> && swap_,
                                           std::optional<unsigned long> && intr_,
                                           std::optional<unsigned long> && soft_irq_,
                                           std::optional<unsigned long> && ctxt_,
                                           std::optional<unsigned long> && btime_,
                                           std::optional<unsigned long> && processes_,
                                           std::optional<unsigned long> && procs_running_,
                                           std::optional<unsigned long> && procs_blocked_)
        : cpus(std::move(cpus_)),
          page(page_),
          swap(swap_),
          intr(intr_),
          soft_irq(soft_irq_),
          ctxt(ctxt_),
          btime(btime_),
          processes(processes_),
          procs_running(procs_running_),
          procs_blocked(procs_blocked_)
    {
    }
}
