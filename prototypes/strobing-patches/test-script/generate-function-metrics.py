import os
import sys
import re
import weakref
from optparse import OptionParser, make_option

sys.path.append(os.environ['PERF_EXEC_PATH'] +
                '/scripts/python/Perf-Trace-Util/lib/Perf/Trace')


SHARE_MODE_AUTO = 0
SHARE_MODE_NONE = 1
SHARE_MODE_DISCARD = 2


# Command line parsing.
def get_options():
    share_modes = {
        'auto': SHARE_MODE_AUTO,
        'none': SHARE_MODE_NONE,
        'discard': SHARE_MODE_DISCARD,
    }
    parser = OptionParser(option_list=[
        make_option("-s", "--share_mode", dest="share_mode", default="auto",
                    help="One of [auto, none, discard]."),
        make_option("-p", "--period", dest="sample_period", default=None,
                    help="Specify the sample period, if not explicit in the attribute name"),
    ])

    (options, args) = parser.parse_args()

    assert options.share_mode in share_modes

    options.share_mode = share_modes[options.share_mode]
    options.sample_period = (
        0 if options.sample_period is None else int(options.sample_period))

    return (options, args)


(options, args) = get_options()


perf_db_export_mode = True
perf_db_export_calls = False
perf_db_export_callchains = False


def put_list(list, id, value):
    list[id] = value


def get_list(list, id):
    return list.get(id, None)


EVENT_ID_SAMPLE_COUNT = -1
EVENT_ID_RATIO = -2
EVENT_ID_RATIO_COUNT = -3


class Event(object):
    def __init__(self, index, event, period, strobe_period):
        assert ((period is None) and (strobe_period is None)) or (
            period is not None)
        self.group = None
        self.index = index
        self.event = event
        self.period = period
        self.strobe_period = strobe_period

    def __repr__(self):
        if self.strobe_period is not None:
            return f"{self.group}:{self.index}:{self.event}({self.period} / {self.strobe_period})"
        elif self.period is not None:
            return f"{self.group}:{self.index}:{self.event}({self.period})"
        else:
            return f"{self.group}:{self.index}:{self.event}"


class Machine(object):
    def __init__(self, pid, dir):
        self.pid = pid
        self.dir = dir

    def __repr__(self):
        return str(self.pid)


class Thread(object):
    def __init__(self, machine, process_thread, pid, tid):
        self.machine = machine
        self.process_thread = process_thread
        self.pid = pid
        self.tid = tid

    def __repr__(self):
        return f"{self.pid}:{self.tid}"


class Comm(object):
    def __init__(self, thread, comm, start, exec):
        self.thread = thread
        self.comm = comm
        self.start = start
        self.exec = exec

    def __repr__(self):
        return self.comm


class DSO(object):
    def __init__(self, machine, short_name, long_name, build_id):
        self.machine = machine
        self.short_name = short_name
        self.long_name = long_name
        self.build_id = build_id

    def __repr__(self):
        return self.long_name

    def __eq__(self, v):
        if not isinstance(v, DSO):
            return False
        return (self.machine == v.machine) and (self.short_name == v.short_name) and (self.long_name == v.long_name) and (self.build_id == v.build_id)

    def __ne__(self, v):
        return not (self == v)


class Symbol(object):
    def __init__(self, dso, start, end, binding, name):
        self.dso = dso
        self.start = start
        self.end = end
        self.binding = binding
        self.name = name

    def __repr__(self):
        if self.is_unknown():
            return str(self.dso)
        return self.name

    def __eq__(self, v):
        if not isinstance(v, Symbol):
            return False

        if self.is_unknown():
            if v.is_unknown():
                return (self.dso == v.dso)
            else:
                return False
        elif v.is_unknown():
            return False
        else:
            return (self.dso == v.dso) and (self.start == v.start) and (self.end == v.end) and (self.binding == v.binding) and (self.name == v.name)

    def __ne__(self, v):
        return not (self == v)

    def is_unknown(self):
        return (self.start == self.end)


class WeakListHolder(object):
    def __init__(self, list):
        self.list = list


class CallPathNode(object):
    def __init__(self, parent, sym, ip):
        self.parent = parent
        self.sym = sym
        self.ip = ip
        self.full_path = None

    def get_full_path(self):
        r = (self.full_path() if self.full_path is not None else None)
        if r is not None:
            return r.list

        if self.parent is not None:
            r = self.parent.get_full_path().copy()
            r.append(self)
        else:
            r = [self]
        self.full_path = weakref.ref(WeakListHolder(r))

        return r

    def __repr__(self):
        if self.parent is not None:
            return f"{self.parent}/{self.sym}"
        return str(self.sym)


def map_event_name(name, event=None):
    raw_event = re.match(r'r([0-9a-fA-F]+)', name)
    if raw_event:
        assert event is None
        return int(name[1:], 16)
    if name.startswith('armv') and event is not None:
        return event
    raise ValueError(f"Unexpected event {name}, {event}")


def split_event_params(params):
    for p in params:
        s = p.split('=')
        assert (len(s) == 1) or (len(s) == 2)
        if len(s) == 1:
            yield (s[0], None)
        else:
            yield (s[0], s[1])


def parse_event(index, evsel_name):
    parts = evsel_name.split('/')

    if len(parts) == 1:
        parts = evsel_name.split(':')
        assert (len(parts) == 1) or (len(parts) == 2)
        return Event(index, map_event_name(parts[0]), None, None)

    assert (len(parts) == 2) or (len(parts) == 3)

    event_name = parts[0].strip()
    params = {k: v for (k, v) in split_event_params(parts[1].split(','))}

    event = (int(params['event'], 0) if 'event' in params else None)
    period = (int(params['period'], 0) if 'period' in params else None)
    config1 = int(params.get('config1', '0'), 0)
    config2 = int(params.get('config2', '0'), 0)
    strobe = int(params.get('strobe', '0'), 0)
    strobe_period = int(params.get('strobe_period', '0'), 0)

    strobe_period = (strobe_period if strobe_period >
                     0 else (config2 if config2 > 0 else None))

    return Event(index, map_event_name(event_name, event), period, strobe_period)


class EventGroup(object):
    def __init__(self, index):
        self.index = index
        self.events_by_code = dict()
        self.strobe_event = None
        self.strobe_threshold = 0

    def __repr__(self):
        return f"Group{self.index}"

    def add_event(self, event):
        event.group = self

        if (event.strobe_period is not None):
            self.has_strobe_events = True
            update_strobe_event = False
            if (self.strobe_event is None):
                update_strobe_event = True
            elif (event.event == 0x11) and (self.strobe_event.event != 0x11):
                update_strobe_event = True
            elif (event.event == 0x08) and (self.strobe_event.event != 0x11) and (self.strobe_event.event != 0x08):
                update_strobe_event = True
            if update_strobe_event:
                self.strobe_event = event
                self.strobe_threshold = (
                    (event.period + event.strobe_period) / 2)

        self.events_by_code[event.event] = event

    def find_event_column_index(self, code):
        event = self.events_by_code.get(code, None)
        if (event is not None):
            return event.index
        return None


class StateTracker(object):
    def __init__(self):
        self.events = dict()
        self.machines = dict()
        self.threads = dict()
        self.comms = dict()
        self.thread_comms = dict()
        self.dsos = dict()
        self.symbols = dict()
        self.call_paths = dict()
        self.column_sample_count = Event(0,
                                         EVENT_ID_SAMPLE_COUNT,
                                         None,
                                         None)
        self.column_ratio = Event(1,
                                  EVENT_ID_RATIO,
                                  None,
                                  None)
        self.column_ratio_count = Event(2,
                                        EVENT_ID_RATIO_COUNT,
                                        None,
                                        None)
        self.columns = [
            self.column_sample_count,
            self.column_ratio,
            self.column_ratio_count,
        ]
        self.has_strobe_events = False
        self.all_event_groups = dict()

    def get_event(self, id):
        return get_list(self.events, id)

    def get_machine(self, id):
        return get_list(self.machines, id)

    def get_thread(self, id):
        return get_list(self.threads, id)

    def get_comm(self, id):
        return get_list(self.comms, id)

    def get_thread_comm(self, id):
        return get_list(self.thread_comms, id)

    def get_dso(self, id):
        return get_list(self.dsos, id)

    def get_symbol(self, id):
        return get_list(self.symbols, id)

    def get_call_path(self, id):
        return get_list(self.call_paths, id)

    def get_default_share_mode(self):
        if self.has_strobe_events:
            return SHARE_MODE_DISCARD
        return SHARE_MODE_NONE

    def add_event(self, db_id, evsel_name):
        index = len(self.columns)
        event = parse_event(index, evsel_name)

        if (len(self.all_event_groups) == 0) and (options.sample_period > 0):
            event.period = options.sample_period

        put_list(self.events,
                 db_id,
                 event)

        self.columns.append(event)

        # new group?
        if (event.period is not None):
            group = EventGroup(db_id)
            group.add_event(event)
            self.all_event_groups[db_id] = group

    def add_machine(self, db_id, pid, dir):
        put_list(self.machines,
                 db_id,
                 Machine(pid, dir))

    def add_thread(self, db_id, machine_id, main_thread_id, pid, tid):
        machine = self.get_machine(machine_id)
        process_thread = (self.get_thread(main_thread_id)
                          if main_thread_id != db_id else None)
        put_list(self.threads,
                 db_id,
                 Thread(machine,
                        process_thread,
                        pid,
                        tid))

    def add_comm(self, db_id, comm, thread_id, start, exec):
        thread = self.get_thread(thread_id)
        put_list(self.comms,
                 db_id,
                 Comm(thread, comm, start, exec))

    def add_thread_comm(self, db_id, comm, thread_id):
        thread = self.get_thread(thread_id)
        put_list(self.thread_comms,
                 db_id,
                 Comm(thread, comm, None, None))

    def add_dso(self, db_id, machine_id, short_name, long_name, build_id):
        machine = self.get_machine(machine_id)
        put_list(self.dsos,
                 db_id,
                 DSO(machine, short_name, long_name, build_id))

    def add_symbol(self, db_id, dso_id, start, end, binding, name):
        dso = self.get_dso(dso_id)
        put_list(self.symbols,
                 db_id,
                 Symbol(dso, start, end, binding, name))

    def add_call_path(self, db_id, parent_id, sym_id, ip):
        parent = self.get_call_path(parent_id)
        sym = self.get_symbol(sym_id)
        put_list(self.call_paths,
                 db_id,
                 CallPathNode(parent, sym, ip))


###################################################################

class Sample(object):
    def __init__(self, event_group, machine, thread, comm, dso, symbol, offset, timestamp, cpu, call_path, values, raw_ids):
        self.event_group = event_group
        self.machine = machine
        self.thread = thread
        self.comm = comm
        self.dso = dso
        self.symbol = symbol
        self.offset = offset
        self.timestamp = timestamp
        self.cpu = cpu
        self.call_path = call_path
        self.values = values
        self.raw_ids = raw_ids

    def __repr__(self):
        return f"{self.machine}, {self.thread}, {self.comm}, {self.dso}, {self.symbol}, {self.offset}, {self.timestamp}, {self.cpu}, {self.values}, {self.raw_ids}"


class SampleBuilder(object):
    def __init__(self, state_tracker):
        self.state_tracker = state_tracker
        self.current_sample_properties = None
        self.current_sample_values = None
        self.earliest_timestamp = None
        self.last_log_time = 0
        self.current_event_group = None
        self.current_raw_ids = set()

    def process_sample(self, db_id, evsel_id, machine_id, thread_id, comm_id, dso_id, sym_id, offset, ip, time, cpu, addr_dso_id, addr_sym_id, addr_offset, addr, period, weight, transaction, data_src, branch_type, in_tx, call_path_id, insn_cnt, cyc_cnt, flags, id, stream_id):
        if (self.earliest_timestamp is None):
            self.earliest_timestamp = time
        else:
            delta = int((time - self.earliest_timestamp) / 60000000000)
            if (delta > self.last_log_time):
                self.last_log_time = delta
                print(f"Trace time: {delta}m")

        event = self.state_tracker.get_event(evsel_id)
        assert event is not None

        current_sample_properties = self.current_sample_properties
        current_sample_values = self.current_sample_values
        new_sample_properties = (machine_id, thread_id, comm_id,
                                 dso_id, sym_id, offset, time, cpu, call_path_id, flags, ip)

        if (self.current_event_group is None):
            self.current_event_group = event.group

        if (current_sample_properties is None) or (current_sample_properties != new_sample_properties) or (event in current_sample_values) or ((event.group is not None) and (self.current_event_group is not None) and (self.current_event_group != event.group)):
            current_event_group = self.current_event_group
            current_raw_ids = self.current_raw_ids
            self.current_event_group = event.group
            self.current_sample_properties = new_sample_properties
            self.current_sample_values = {event: period}
            self.current_raw_ids = {id, stream_id}
            return self.make_sample(current_event_group, current_sample_properties, current_sample_values, current_raw_ids)

        self.current_sample_values[event] = period
        self.current_raw_ids.add(id)
        self.current_raw_ids.add(stream_id)
        return None

    def make_sample(self, group, properties, values, raw_ids):
        assert (group is not None)
        assert ((properties is None) and (values is None)) or (
            (properties is not None) and (values is not None))

        if ((properties is None) and (values is None)):
            return None

        (machine_id, thread_id, comm_id, dso_id, sym_id,
         offset, time, cpu, call_path_id, flags, ip) = properties

        machine = self.state_tracker.get_machine(machine_id)
        thread = self.state_tracker.get_thread(thread_id)
        comm = self.state_tracker.get_comm(comm_id)
        dso = self.state_tracker.get_dso(dso_id)
        symbol = self.state_tracker.get_symbol(sym_id)
        call_path = self.state_tracker.get_call_path(call_path_id)

        if (call_path is not None) and (call_path.sym is not None):
            symbol = call_path.sym

        for (e, v) in values.items():
            assert (e.group is None) or (
                e.group == group), f"group is {group} vs {e.group}"
            if e.group is None:
                group.add_event(e)

        return Sample(group, machine, thread, comm, dso, symbol, offset, time, cpu, call_path, values, raw_ids)


###################################################################
class ThrottleTracker(object):
    def __init__(self):
        self.events = list()
        self.throttled_ids = set()
        self.unthrottled_ids = set()
        self.last_sample_after_throttle = set()
        self.last_timestamp = {}

    def throttle_event(self, time, stream_id,  throttled):
        self.events.append((time, stream_id, throttled))

    def is_sample_throttled(self, time, raw_ids):
        self.update_until(time)
        result = False
        for id in raw_ids:
            result = result or self.is_id_throttled(time, id)
        return result

    def is_id_throttled(self, time, id):

        last_timestamp = self.last_timestamp.get(id,  None)
        assert (last_timestamp is None) or (time >= last_timestamp)
        self.last_timestamp[id] = time

        is_throttled = (id in self.throttled_ids)
        is_unthrottled = (id in self.unthrottled_ids)
        seen_last_sample_after_throttle = (
            id in self.last_sample_after_throttle)

        if (not is_throttled) and (not is_unthrottled):
            return False

        if is_throttled and is_unthrottled:
            self.throttled_ids.discard(id)
            self.unthrottled_ids.discard(id)
            self.last_sample_after_throttle.discard(id)
            return True

        if not seen_last_sample_after_throttle:
            self.last_sample_after_throttle.add(id)
            return False

        return True

    def update_until(self, upto_time):
        while (len(self.events) > 0):
            (time, stream_id, is_throttled) = self.events[0]

            if time >= upto_time:
                break

            self.events.pop(0)

            last_timestamp = self.last_timestamp.get(stream_id,  None)
            assert (last_timestamp is None) or (time >= last_timestamp)
            self.last_timestamp[stream_id] = time

            if is_throttled:
                self.throttled_ids.add(stream_id)
                self.unthrottled_ids.discard(stream_id)
            else:
                self.throttled_ids.add(stream_id)
                self.unthrottled_ids.add(stream_id)


class SampleProcessor(object):
    def __init__(self, options, state_tracker, throttle_tracker):
        self.share_mode = options.share_mode
        self.state_tracker = state_tracker
        self.throttle_tracker = throttle_tracker
        self.accumulated_values = dict()
        self.total_values = list()
        self.strobed_samples = dict()
        self.discard_counter = 0
        self.sample_counter = 0
        self.strobe_counter = 0
        self.throttle_counter = 0

        assert self.share_mode in {SHARE_MODE_AUTO,
                                   SHARE_MODE_NONE,
                                   SHARE_MODE_DISCARD}

    def add_sample(self, sample):
        self.share_mode = (self.share_mode
                           if self.share_mode is not SHARE_MODE_AUTO
                           else self.state_tracker.get_default_share_mode())
        assert self.share_mode in {SHARE_MODE_NONE,
                                   SHARE_MODE_DISCARD}

        # Discard the sample if the CPU was throttled
        # - the first sample after a throttle is not usable - the non-sampling events are left running, whilst the sampling event is not, meaning you end up with garbage like 100000 IPC :-(
        if self.throttle_tracker.is_sample_throttled(sample.timestamp, sample.raw_ids):
            self.throttle_counter += 1
            self.strobed_samples[sample.thread] = None
            return

        # Discard it if it is above the strobe threshold - the cycle count is way too large. discard the counters
        if (sample.event_group.strobe_event is not None) and (sample.values.get(sample.event_group.strobe_event, 0) > sample.event_group.strobe_threshold):
            self.strobe_counter += 1
            if sample.symbol is not None:
                self.strobed_samples[sample.thread] = sample
            else:
                self.strobed_samples[sample.thread] = None
            return

        # Discard it if it has no symbol
        if sample.symbol is None:
            self.strobed_samples[sample.thread] = None
            return

        # is it the same symbol as the last sample
        last_strobe_sample = self.strobed_samples.get(sample.thread)
        is_same_symbol = ((last_strobe_sample is not None) and (
            last_strobe_sample.symbol == sample.symbol))

        # Discard events where the symbols don't match
        if (self.share_mode == SHARE_MODE_DISCARD) and (not is_same_symbol):
            # discard if they are not in the same symbol
            self.discard_counter += 1
            # but also increment the samples sample count
            self.accumulate_sample(sample, {}, 1, True)
        else:
            # assign all to the current sample
            self.accumulate_sample(sample,
                                   sample.values,
                                   None,
                                   True)
            self.sample_counter += 1

        # save for the next round
        self.strobed_samples[sample.thread] = (sample if (
            self.share_mode != SHARE_MODE_NONE) else None)

    def accumulate_sample(self, sample, values, ratio, inc_n):
        self.accumulate_symbol(sample.symbol, values, ratio, inc_n)

    def accumulate_symbol(self, symbol, values, ratio, inc_n):
        key = (symbol.name
               if not symbol.is_unknown()
               else symbol.dso.short_name)
        self.accumulate_key(key, values, ratio, inc_n)

    def accumulate_key(self, key, values, ratio, inc_n):
        if not key in self.accumulated_values:
            self.accumulated_values[key] = (
                [0] * len(self.state_tracker.columns)
            )
        accumulated = self.accumulated_values[key]

        extend(self.total_values, len(self.state_tracker.columns))
        extend(accumulated, len(self.state_tracker.columns))

        for (e, v) in values.items():
            accumulated[e.index] += v
            self.total_values[e.index] += v

        if inc_n:
            accumulated[self.state_tracker.column_sample_count.index] += 1
            self.total_values[self.state_tracker.column_sample_count.index] += 1

        if ratio is not None:
            accumulated[self.state_tracker.column_ratio.index] += ratio
            accumulated[self.state_tracker.column_ratio_count.index] += 1
            self.total_values[self.state_tracker.column_ratio.index] += ratio
            self.total_values[self.state_tracker.column_ratio_count.index] += 1


    #
    # Simplified single-group metrics
    #

    def add_group_columns_single(self, columns, sort_columns, group):
        # common
        ndx_cycles = group.find_event_column_index(0x11)
        ndx_br_mispred = group.find_event_column_index(0x10)
        ndx_l1d_access = group.find_event_column_index(0x04)

        # n1
        ndx_inst_ret = group.find_event_column_index(0x08)
        ndx_inst_spec = group.find_event_column_index(0x1b)
        ndx_l1d_refill = group.find_event_column_index(0x03)
        ndx_backed_stall = group.find_event_column_index(0x24)

        # v1
        ndx_l1d_miss = group.find_event_column_index(0x39)
        ndx_op_ret = group.find_event_column_index(0x3a)
        ndx_op_spec = group.find_event_column_index(0x3b)
        ndx_stall_slot = group.find_event_column_index(0x3f)

        if ndx_cycles is not None:
            sort_columns.append(ndx_cycles)
        if ndx_inst_ret is not None:
            sort_columns.append(ndx_inst_ret)
        if ndx_inst_spec is not None:
            sort_columns.append(ndx_inst_spec)
        if ndx_op_ret is not None:
            sort_columns.append(ndx_op_ret)
        if ndx_op_spec is not None:
            sort_columns.append(ndx_op_spec)

        # make columns for display
        if (ndx_cycles is not None) and (ndx_inst_ret is not None):
            columns.append((
                'CPI',
                lambda v: ((v[ndx_cycles] / v[ndx_inst_ret])
                           if v[ndx_inst_ret] > 0 else 0)
            ))

        if (ndx_cycles is not None) and (ndx_op_ret is not None):
            columns.append((
                'CPuO',
                lambda v: ((v[ndx_cycles] / v[ndx_op_ret])
                           if v[ndx_op_ret] > 0 else 0)
            ))

        if (ndx_inst_spec is not None) and (ndx_inst_ret is not None):
            columns.append((
                '% Retiring Insns',
                lambda v: (((100 * v[ndx_inst_ret]) / v[ndx_inst_spec])
                           if v[ndx_inst_spec] > 0 else 0)
            ))

        if (ndx_cycles is not None) and (ndx_op_ret is not None) and (ndx_op_spec is not None) and (ndx_stall_slot is not None):
            columns.append((
                '% Retiring Slots',
                lambda v: ((100 * (((v[ndx_op_ret] / v[ndx_op_spec])) * (1 - (v[ndx_stall_slot] / (v[ndx_cycles] * 8)))))
                           if (v[ndx_cycles] > 0) and (v[ndx_op_spec] > 0) else 0)
            ))

        if (ndx_op_spec is not None) and (ndx_op_ret is not None):
            columns.append((
                '% Retiring uOps',
                lambda v: (((100 * v[ndx_op_ret]) / v[ndx_op_spec])
                           if v[ndx_op_spec] > 0 else 0)
            ))

        if (ndx_cycles is not None) and (ndx_op_ret is not None) and (ndx_op_spec is not None) and (ndx_stall_slot is not None) and (ndx_br_mispred is not None):
            columns.append((
                '% Bad Speculation',
                lambda v: ((100 * (((1-(v[ndx_op_ret] / v[ndx_op_spec])) * (1 - (v[ndx_stall_slot] / (v[ndx_cycles] * 8)))) + ((v[ndx_br_mispred] * 4) / v[ndx_cycles])))
                           if (v[ndx_cycles] > 0) and (v[ndx_op_spec] > 0) else 0)
            ))

        if (ndx_backed_stall is not None) and (ndx_cycles is not None):
            columns.append((
                '% Backend Stalls',
                lambda v: (((100 * v[ndx_backed_stall]) / v[ndx_cycles])
                           if v[ndx_cycles] > 0 else 0)
            ))

        if (ndx_stall_slot is not None) and (ndx_cycles is not None):
            columns.append((
                '% Stalls',
                lambda v: (((100 * v[ndx_stall_slot]) / (v[ndx_cycles] * 8))
                           if v[ndx_cycles] > 0 else 0)
            ))

        if (ndx_br_mispred is not None) and (ndx_inst_ret is not None):
            columns.append((
                'Mispredicts / KI',
                lambda v: (((1000 * v[ndx_br_mispred]) / v[ndx_inst_ret])
                           if v[ndx_inst_ret] > 0 else 0)
            ))

        if (ndx_br_mispred is not None) and (ndx_op_ret is not None):
            columns.append((
                'Mispredicts / KuOp',
                lambda v: (((1000 * v[ndx_br_mispred]) / v[ndx_op_ret])
                           if v[ndx_op_ret] > 0 else 0)
            ))

        if (ndx_l1d_refill is not None) and (ndx_inst_ret is not None):
            columns.append((
                'L1D Refills / KI',
                lambda v: (((1000 * v[ndx_l1d_refill]) / v[ndx_inst_ret])
                           if v[ndx_inst_ret] > 0 else 0)
            ))

        if (ndx_l1d_refill is not None) and (ndx_l1d_access is not None):
            columns.append((
                '% L1D Refills / Access',
                lambda v: (((100 * v[ndx_l1d_refill]) / v[ndx_l1d_access])
                           if v[ndx_l1d_access] > 0 else 0)
            ))

        if (ndx_l1d_miss is not None) and (ndx_inst_ret is not None):
            columns.append((
                'L1D Misses / KI',
                lambda v: (((1000 * v[ndx_l1d_miss]) / v[ndx_inst_ret])
                           if v[ndx_inst_ret] > 0 else 0)
            ))

        if (ndx_l1d_miss is not None) and (ndx_op_ret is not None):
            columns.append((
                'L1D Misses / KuOp',
                lambda v: (((1000 * v[ndx_l1d_miss]) / v[ndx_op_ret])
                           if v[ndx_op_ret] > 0 else 0)
            ))

        if (ndx_l1d_miss is not None) and (ndx_l1d_access is not None):
            columns.append((
                '% L1D Misses / Access',
                lambda v: (((100 * v[ndx_l1d_miss]) / v[ndx_l1d_access])
                           if v[ndx_l1d_access] > 0 else 0)
            ))

    #
    # Support for the N1 topdown metrics multiplexed groups
    #

    def add_group_columns_top_down(self, columns, sort_columns, group):
        multiplier_percent = 100
        multiplier_mpki = 1000

        ndx_0_cpu_cycles = group.find_event_column_index(0x0011)
        ndx_0_inst_retired = group.find_event_column_index(0x0008)
        ndx_0_stall_frontend = group.find_event_column_index(0x0023)
        ndx_0_stall_backend = group.find_event_column_index(0x0024)

        if (ndx_0_inst_retired is not None) and (ndx_0_cpu_cycles is not None):
            columns.append((
                'Cycles Per Instruction',
                lambda v: (((v[ndx_0_cpu_cycles] / v[ndx_0_inst_retired]))
                           if (v[ndx_0_inst_retired] > 0) else 0),
            ))

        if (ndx_0_stall_backend is not None) and (ndx_0_cpu_cycles is not None):
            columns.append((
                'Backend Stalled Cycles',
                lambda v: ((((v[ndx_0_stall_backend] / v[ndx_0_cpu_cycles])
                           * multiplier_percent)) if (v[ndx_0_cpu_cycles] > 0) else 0),
            ))

        if (ndx_0_stall_frontend is not None) and (ndx_0_cpu_cycles is not None):
            columns.append((
                'Frontend Stalled Cycles',
                lambda v: ((((v[ndx_0_stall_frontend] / v[ndx_0_cpu_cycles])
                           * multiplier_percent)) if (v[ndx_0_cpu_cycles] > 0) else 0),
            ))

        ndx_1_cpu_cycles = group.find_event_column_index(0x0011)
        ndx_1_l1d_cache_refill = group.find_event_column_index(0x0003)
        ndx_1_l1d_cache = group.find_event_column_index(0x0004)
        ndx_1_l1d_tlb_refill = group.find_event_column_index(0x0005)
        ndx_1_inst_retired = group.find_event_column_index(0x0008)
        ndx_1_l1d_tlb = group.find_event_column_index(0x0025)
        ndx_1_dtlb_walk = group.find_event_column_index(0x0034)

        if (ndx_1_l1d_cache_refill is not None) and (ndx_1_l1d_cache is not None):
            columns.append((
                'L1D Cache Miss Ratio',
                lambda v: (((v[ndx_1_l1d_cache_refill] / v[ndx_1_l1d_cache]))
                           if (v[ndx_1_l1d_cache] > 0) else 0),
            ))

        if (ndx_1_inst_retired is not None) and (ndx_1_dtlb_walk is not None):
            columns.append((
                'DTLB MPKI',
                lambda v: ((((v[ndx_1_dtlb_walk] / v[ndx_1_inst_retired])
                           * multiplier_mpki)) if (v[ndx_1_inst_retired] > 0) else 0),
            ))

        if (ndx_1_l1d_tlb_refill is not None) and (ndx_1_l1d_tlb is not None):
            columns.append((
                'L1 Data TLB Miss Ratio',
                lambda v: (((v[ndx_1_l1d_tlb_refill] / v[ndx_1_l1d_tlb]))
                           if (v[ndx_1_l1d_tlb] > 0) else 0),
            ))

        if (ndx_1_inst_retired is not None) and (ndx_1_l1d_cache_refill is not None):
            columns.append((
                'L1D Cache MPKI',
                lambda v: ((((v[ndx_1_l1d_cache_refill] / v[ndx_1_inst_retired])
                           * multiplier_mpki)) if (v[ndx_1_inst_retired] > 0) else 0),
            ))

        if (ndx_1_inst_retired is not None) and (ndx_1_l1d_tlb_refill is not None):
            columns.append((
                'L1 Data TLB MPKI',
                lambda v: ((((v[ndx_1_l1d_tlb_refill] / v[ndx_1_inst_retired])
                           * multiplier_mpki)) if (v[ndx_1_inst_retired] > 0) else 0),
            ))

        if (ndx_1_l1d_tlb is not None) and (ndx_1_dtlb_walk is not None):
            columns.append((
                'DTLB Walk Ratio',
                lambda v: (((v[ndx_1_dtlb_walk] / v[ndx_1_l1d_tlb]))
                           if (v[ndx_1_l1d_tlb] > 0) else 0),
            ))

        ndx_2_cpu_cycles = group.find_event_column_index(0x0011)
        ndx_2_l1i_cache_refill = group.find_event_column_index(0x0001)
        ndx_2_l1i_tlb_refill = group.find_event_column_index(0x0002)
        ndx_2_inst_retired = group.find_event_column_index(0x0008)
        ndx_2_l1i_cache = group.find_event_column_index(0x0014)
        ndx_2_l1i_tlb = group.find_event_column_index(0x0026)
        ndx_2_itlb_walk = group.find_event_column_index(0x0035)

        if (ndx_2_inst_retired is not None) and (ndx_2_itlb_walk is not None):
            columns.append((
                'ITLB MPKI',
                lambda v: ((((v[ndx_2_itlb_walk] / v[ndx_2_inst_retired])
                           * multiplier_mpki)) if (v[ndx_2_inst_retired] > 0) else 0),
            ))

        if (ndx_2_l1i_tlb is not None) and (ndx_2_l1i_tlb_refill is not None):
            columns.append((
                'L1 Instruction TLB Miss Ratio',
                lambda v: (((v[ndx_2_l1i_tlb_refill] / v[ndx_2_l1i_tlb]))
                           if (v[ndx_2_l1i_tlb] > 0) else 0),
            ))

        if (ndx_2_l1i_cache is not None) and (ndx_2_l1i_cache_refill is not None):
            columns.append((
                'L1I Cache Miss Ratio',
                lambda v: (((v[ndx_2_l1i_cache_refill] / v[ndx_2_l1i_cache]))
                           if (v[ndx_2_l1i_cache] > 0) else 0),
            ))

        if (ndx_2_inst_retired is not None) and (ndx_2_l1i_tlb_refill is not None):
            columns.append((
                'L1 Instruction TLB MPKI',
                lambda v: ((((v[ndx_2_l1i_tlb_refill] / v[ndx_2_inst_retired])
                           * multiplier_mpki)) if (v[ndx_2_inst_retired] > 0) else 0),
            ))

        if (ndx_2_inst_retired is not None) and (ndx_2_l1i_cache_refill is not None):
            columns.append((
                'L1I Cache MPKI',
                lambda v: ((((v[ndx_2_l1i_cache_refill] / v[ndx_2_inst_retired])
                           * multiplier_mpki)) if (v[ndx_2_inst_retired] > 0) else 0),
            ))

        if (ndx_2_l1i_tlb is not None) and (ndx_2_itlb_walk is not None):
            columns.append((
                'ITLB Walk Ratio',
                lambda v: (((v[ndx_2_itlb_walk] / v[ndx_2_l1i_tlb]))
                           if (v[ndx_2_l1i_tlb] > 0) else 0),
            ))

        ndx_3_cpu_cycles = group.find_event_column_index(0x0011)
        ndx_3_inst_retired = group.find_event_column_index(0x0008)
        ndx_3_inst_spec = group.find_event_column_index(0x001b)
        ndx_3_br_retired = group.find_event_column_index(0x0021)
        ndx_3_br_mis_pred_retired = group.find_event_column_index(0x0022)
        ndx_3_br_immed_spec = group.find_event_column_index(0x0078)
        ndx_3_br_indirect_spec = group.find_event_column_index(0x007a)

        if (ndx_3_inst_retired is not None) and (ndx_3_br_mis_pred_retired is not None):
            columns.append((
                'Branch MPKI',
                lambda v: ((((v[ndx_3_br_mis_pred_retired] / v[ndx_3_inst_retired])
                           * multiplier_mpki)) if (v[ndx_3_inst_retired] > 0) else 0),
            ))

        if (ndx_3_br_mis_pred_retired is not None) and (ndx_3_br_retired is not None):
            columns.append((
                'Branch Misprediction Ratio',
                lambda v: (((v[ndx_3_br_mis_pred_retired] / v[ndx_3_br_retired]))
                           if (v[ndx_3_br_retired] > 0) else 0),
            ))

        if (ndx_3_br_indirect_spec is not None) and (ndx_3_inst_spec is not None) and (ndx_3_br_immed_spec is not None):
            columns.append((
                'Branch Operations Percentage',
                lambda v: (((((v[ndx_3_br_immed_spec] + v[ndx_3_br_indirect_spec]) /
                           v[ndx_3_inst_spec]) * multiplier_percent)) if (v[ndx_3_inst_spec] > 0) else 0),
            ))

        ndx_4_cpu_cycles = group.find_event_column_index(0x0011)
        ndx_4_inst_retired = group.find_event_column_index(0x0008)
        ndx_4_l2d_cache = group.find_event_column_index(0x0016)
        ndx_4_l2d_cache_refill = group.find_event_column_index(0x0017)
        ndx_4_l2d_tlb_refill = group.find_event_column_index(0x002d)
        ndx_4_l2d_tlb = group.find_event_column_index(0x002f)

        if (ndx_4_l2d_tlb_refill is not None) and (ndx_4_l2d_tlb is not None):
            columns.append((
                'L2 Unified TLB Miss Ratio',
                lambda v: (((v[ndx_4_l2d_tlb_refill] / v[ndx_4_l2d_tlb]))
                           if (v[ndx_4_l2d_tlb] > 0) else 0),
            ))

        if (ndx_4_l2d_cache_refill is not None) and (ndx_4_l2d_cache is not None):
            columns.append((
                'L2 Cache Miss Ratio',
                lambda v: (((v[ndx_4_l2d_cache_refill] / v[ndx_4_l2d_cache]))
                           if (v[ndx_4_l2d_cache] > 0) else 0),
            ))

        if (ndx_4_l2d_cache_refill is not None) and (ndx_4_inst_retired is not None):
            columns.append((
                'L2 Cache MPKI',
                lambda v: ((((v[ndx_4_l2d_cache_refill] / v[ndx_4_inst_retired])
                           * multiplier_mpki)) if (v[ndx_4_inst_retired] > 0) else 0),
            ))

        if (ndx_4_inst_retired is not None) and (ndx_4_l2d_tlb_refill is not None):
            columns.append((
                'L2 Unified TLB MPKI',
                lambda v: ((((v[ndx_4_l2d_tlb_refill] / v[ndx_4_inst_retired])
                           * multiplier_mpki)) if (v[ndx_4_inst_retired] > 0) else 0),
            ))

        ndx_5_cpu_cycles = group.find_event_column_index(0x0011)
        ndx_5_inst_spec = group.find_event_column_index(0x001b)
        ndx_5_dp_spec = group.find_event_column_index(0x0073)
        ndx_5_ase_spec = group.find_event_column_index(0x0074)
        ndx_5_vfp_spec = group.find_event_column_index(0x0075)
        ndx_5_crypto_spec = group.find_event_column_index(0x0077)

        if (ndx_5_dp_spec is not None) and (ndx_5_inst_spec is not None):
            columns.append((
                'Integer Operations Percentage',
                lambda v: ((((v[ndx_5_dp_spec] / v[ndx_5_inst_spec]) *
                           multiplier_percent)) if (v[ndx_5_inst_spec] > 0) else 0),
            ))

        if (ndx_5_crypto_spec is not None) and (ndx_5_inst_spec is not None):
            columns.append((
                'Crypto Operations Percentage',
                lambda v: ((((v[ndx_5_crypto_spec] / v[ndx_5_inst_spec]) *
                           multiplier_percent)) if (v[ndx_5_inst_spec] > 0) else 0),
            ))

        if (ndx_5_ase_spec is not None) and (ndx_5_inst_spec is not None):
            columns.append((
                'Advanced SIMD Operations Percentage',
                lambda v: ((((v[ndx_5_ase_spec] / v[ndx_5_inst_spec]) *
                           multiplier_percent)) if (v[ndx_5_inst_spec] > 0) else 0),
            ))

        if (ndx_5_inst_spec is not None) and (ndx_5_vfp_spec is not None):
            columns.append((
                'Floating Point Operations Percentage',
                lambda v: ((((v[ndx_5_vfp_spec] / v[ndx_5_inst_spec]) *
                           multiplier_percent)) if (v[ndx_5_inst_spec] > 0) else 0),
            ))

        ndx_6_cpu_cycles = group.find_event_column_index(0x0011)
        ndx_6_inst_retired = group.find_event_column_index(0x0008)
        ndx_6_ll_cache_rd = group.find_event_column_index(0x0036)
        ndx_6_ll_cache_miss_rd = group.find_event_column_index(0x0037)

        if (ndx_6_ll_cache_rd is not None) and (ndx_6_ll_cache_miss_rd is not None):
            columns.append((
                'LL Cache Read Miss Ratio',
                lambda v: (((v[ndx_6_ll_cache_miss_rd] / v[ndx_6_ll_cache_rd]))
                           if (v[ndx_6_ll_cache_rd] > 0) else 0),
            ))

        if (ndx_6_ll_cache_miss_rd is not None) and (ndx_6_inst_retired is not None):
            columns.append((
                'LL Cache Read MPKI',
                lambda v: ((((v[ndx_6_ll_cache_miss_rd] / v[ndx_6_inst_retired])
                           * multiplier_mpki)) if (v[ndx_6_inst_retired] > 0) else 0),
            ))

        if (ndx_6_ll_cache_rd is not None) and (ndx_6_ll_cache_miss_rd is not None):
            columns.append((
                'LL Cache Read Hit Ratio',
                lambda v: ((((v[ndx_6_ll_cache_rd] - v[ndx_6_ll_cache_miss_rd]) /
                           v[ndx_6_ll_cache_rd])) if (v[ndx_6_ll_cache_rd] > 0) else 0),
            ))

        ndx_7_cpu_cycles = group.find_event_column_index(0x0011)
        ndx_7_inst_spec = group.find_event_column_index(0x001b)
        ndx_7_ld_spec = group.find_event_column_index(0x0070)
        ndx_7_st_spec = group.find_event_column_index(0x0071)

        if (ndx_7_ld_spec is not None) and (ndx_7_inst_spec is not None):
            columns.append((
                'Load Operations Percentage',
                lambda v: ((((v[ndx_7_ld_spec] / v[ndx_7_inst_spec]) *
                           multiplier_percent)) if (v[ndx_7_inst_spec] > 0) else 0),
            ))

        if (ndx_7_inst_spec is not None) and (ndx_7_st_spec is not None):
            columns.append((
                'Store Operations Percentage',
                lambda v: ((((v[ndx_7_st_spec] / v[ndx_7_inst_spec]) *
                           multiplier_percent)) if (v[ndx_7_inst_spec] > 0) else 0),
            ))

    def add_group_columns(self, columns, sort_columns, group, multiple_groups):
        if multiple_groups:
            self.add_group_columns_top_down(columns, sort_columns, group)
        else:
            self.add_group_columns_single(columns, sort_columns, group)

    def complete(self):
        print(
            f"Completed processing: # samples = {self.sample_counter} / # strobes = {self.strobe_counter} / # throttles = {self.throttle_counter} / # discards = {self.discard_counter} / share_mode = {self.share_mode}")

        # This accumulates the column indexes on which to sort the data
        sort_columns = [self.state_tracker.column_sample_count.index]
        # This accumulates the columns themselves
        columns = [
            (
                '# Samples',
                lambda v: v[self.state_tracker.column_sample_count.index]
            )
        ]

        # Process event groups
        for (event_db_id, group) in self.state_tracker.all_event_groups.items():
            self.add_group_columns(columns, sort_columns, group, (len(
                self.state_tracker.all_event_groups) > 1))

        sorted_rows = self.create_rows(sort_columns)

        for c in self.state_tracker.columns:
            if (c != self.state_tracker.column_sample_count) and (c != self.state_tracker.column_ratio):
                self.add_self_columns(columns,  c)

        if (self.share_mode == SHARE_MODE_DISCARD):
            columns.append((
                'Discard Ratio',
                lambda v: (((100 * v[self.state_tracker.column_ratio.index]) / v[self.state_tracker.column_sample_count.index])
                           if v[self.state_tracker.column_sample_count.index] > 0 else 0)
            ))

        # write output
        rcols = ['Symbol']
        rcols.extend([
            n for (n, l) in columns
        ])

        print('\t'.join(rcols))

        n_i = len(self.state_tracker.columns)

        for (s, v) in sorted_rows:
            rcols = [s]
            rcols.extend([
                str(l(extend(v, n_i))) for (n, l) in columns
            ])
            print('\t'.join(rcols))

    def create_rows(self, sort_columns):
        result = list(self.accumulated_values.items())
        result.sort(key=lambda r:  self.sort_row(
            r, sort_columns), reverse=True)
        return result

    def sort_row(self, row, sort_columns):
        (s, v) = row
        return [v[c] if c < len(v) else 0 for c in sort_columns]

    def add_self_columns(self, columns, c):
        if (c.event >= 0):
            columns.append((
                f'% Total: r{c.event:04x}',
                lambda v: (((100 * v[c.index]) / self.total_values[c.index]) if (c.index < len(v)) and (
                    c.index < len(self.total_values) and self.total_values[c.index] > 0) else 0)
            ))
            columns.append((
                f'# r{c.event:04x}',
                lambda v: (v[c.index] if c.index < len(v) else 0)
            ))


def extend(v, n):
    if len(v) < n:
        v.extend([0] * (n - len(v)))
    return v


###################################################################
state_tracker = StateTracker()
sample_build = SampleBuilder(state_tracker)
throttle_tracker = ThrottleTracker()
sample_processor = SampleProcessor(options, state_tracker, throttle_tracker)


###################################################################


def evsel_table(db_id, evsel_name):
    state_tracker.add_event(db_id, evsel_name)


def machine_table(db_id, pid, dir):
    state_tracker.add_machine(db_id, pid, dir)


def thread_table(db_id, machine_id, main_thread_Id, pid, tid):
    state_tracker.add_thread(db_id, machine_id, main_thread_Id, pid, tid)


def comm_table(db_id, comm, thread_id, start, exec):
    state_tracker.add_comm(db_id, comm, thread_id, start, exec)


def comm_thread_table(db_id, comm, thread_id):
    state_tracker.add_thread_comm(db_id, comm, thread_id)


def dso_table(db_id, machine_id, short_name, long_name, build_id):
    state_tracker.add_dso(db_id, machine_id, short_name, long_name, build_id)


def symbol_table(db_id, dso_id, start, end, binding, name):
    state_tracker.add_symbol(db_id, dso_id, start, end, binding, name)


def call_path_table(db_id, parent_id, sym_id, ip):
    state_tracker.add_call_path(db_id, parent_id, sym_id, ip)


def sample_table(db_id, evsel_id, machine_id, thread_id, comm_id, dso_id, sym_id, offset, ip, time, cpu, addr_dso_id, addr_sym_id, addr_offset, addr, period, weight, transaction, data_src, branch_type, in_tx, call_path_id, insn_cnt, cyc_cnt, flags, id, stream_id):
    complete_sample = sample_build.process_sample(db_id, evsel_id, machine_id, thread_id, comm_id, dso_id, sym_id, offset, ip, time, cpu, addr_dso_id,
                                                  addr_sym_id, addr_offset, addr, period, weight, transaction, data_src, branch_type, in_tx, call_path_id, insn_cnt, cyc_cnt, flags, id, stream_id)
    if complete_sample is None:
        return

    sample_processor.add_sample(complete_sample)


def throttle(time, id, stream_id, cpu, pid, tid):
    throttle_tracker.throttle_event(time,  stream_id,  True)


def unthrottle(time, id, stream_id, cpu, pid, tid):
    throttle_tracker.throttle_event(time,  stream_id,  False)


def trace_end():
    sample_processor.complete()
