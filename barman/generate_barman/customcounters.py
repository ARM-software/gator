# Copyright (C) 2023 by Arm Limited
#
# SPDX-License-Identifier: BSD-3-Clause

from functools import reduce
from typing import List

from .config import *
from .sourceprocessor import SourceProcessor, TokenDirectiveProcessor

def _escape_string(value: str) -> str:
    arr = bytearray()
    for c in value:
        if c == '"':
            arr.append('\\')
            arr.append('"')
        elif c == '\\':
            arr.append('\\')
            arr.append('\\')
        elif c < ' ':
            arr.append(' ')
        elif ord(c) < 0x80:
            arr.append(ord(c))
        else:
            arr.append(' ')
    return arr.decode('ascii')


def _bool_to_bm_str(value: bool) -> str:
    return "BM_TRUE" if value else "BM_FALSE"

def _series_composition_to_bm_str(value: SeriesComposition) -> str:
    if value == SeriesComposition.LOG10:
        return 'BM_SERIES_COMPOSITION_LOG10'
    elif value == SeriesComposition.OVERLAY:
        return 'BM_SERIES_COMPOSITION_OVERLAY'
    elif value == SeriesComposition.STACKED:
        return 'BM_SERIES_COMPOSITION_STACKED'
    else:
        raise ValueError('Unknown series composition: {}'.format(value))


def _rendering_type_to_bm_str(value: RenderingType) -> str:
    if value == RenderingType.BAR:
        return 'BM_RENDERING_TYPE_BAR'
    elif value == RenderingType.FILLED:
        return 'BM_RENDERING_TYPE_FILLED'
    elif value == RenderingType.LINE:
        return 'BM_RENDERING_TYPE_LINE'
    else:
        raise ValueError('Unknown rendering type: {}'.format(value))


def _counter_class_to_bm_str(value: CounterClass) -> str:
    if value == CounterClass.ABSOLUTE:
        return 'BM_SERIES_CLASS_ABSOLUTE'
    elif value == CounterClass.DELTA:
        return 'BM_SERIES_CLASS_DELTA'
    elif value == CounterClass.INCIDENT:
        return 'BM_SERIES_CLASS_INCIDENT'
    else:
        raise ValueError('Unknown counter class: {}'.format(value))


def _counter_display_to_bm_str(value: CounterDisplay) -> str:
    if value == CounterDisplay.ACCUMULATE:
        return 'BM_SERIES_DISPLAY_ACCUMULATE'
    elif value == CounterDisplay.AVERAGE:
        return 'BM_SERIES_DISPLAY_AVERAGE'
    elif value == CounterDisplay.HERTZ:
        return 'BM_SERIES_DISPLAY_HERTZ'
    elif value == CounterDisplay.MAXIMUM:
        return 'BM_SERIES_DISPLAY_MAXIMUM'
    elif value == CounterDisplay.MINIMUM:
        return 'BM_SERIES_DISPLAY_MINIMUM'
    else:
        raise ValueError('Unknown counter display type: {}'.format(value))


def _generate_sampled_series_declaration(lines: List[str], chart: CustomChart, series: CustomChartSeries):
    func_name = 'barman_cc_{}_{}_sample_now' \
        .format(chart.get_name_identifier(), series.get_name_identifier()) \
        .lower()

    lines.append('/** Custom counter sampling function for custom counter {} - {} */'
        .format(chart.name, series.name))
    lines.append('BM_NONNULL((1))')
    lines.append('extern bm_bool {}(bm_uint64 * value_out);'.format(func_name))
    lines.append('')


def _generate_update_series_declaration(lines: List[str], chart: CustomChart, series: CustomChartSeries):
    func_name = 'barman_cc_{}_{}_update_value' \
        .format(chart.get_name_identifier(), series.get_name_identifier()) \
        .lower()
    now_func_name = func_name + '_now'
    per_core_param = ", bm_uint32 core" if chart.per_cpu else ""

    lines.append('/** Custom counter update function for custom counter {} - {}'
        .format(chart.name, series.name))
    lines.append(' * - This version takes a timestamp and possibly also core value as arguments')
    lines.append(' */')
    lines.append('bm_bool {}(bm_uint64 timestamp{}, bm_uint64 value);'.format(func_name, per_core_param))
    lines.append('')
    lines.append('/** Custom counter update function for custom counter {} - {}'
        .format(chart.name, series.name))
    lines.append(' * - This version takes just the value and sends the current timestamp and core')
    lines.append(' */')
    lines.append('bm_bool {}(bm_uint64 value);'.format(now_func_name))
    lines.append('')


def _generate_series_definition(lines: List[str], chart: CustomChart, chart_index: int, series: CustomChartSeries, series_index: int, constant_name: str):
    colour_str = '~0u' if series.colour is None else '0x{:x}'.format(series.colour & 0xFFFFFF)
    sample_fn_name = 'barman_cc_{}_{}_sample_now'.format(chart.get_name_identifier(), series.get_name_identifier())

    # write out the series structure
    template = 'static const struct bm_custom_counter_chart_series {} = {{{}, "{}", "{}", "{}", {}, {}, {}, {}, {}}};'
    lines.append(template.format(
        constant_name,
        chart_index,
        _escape_string(series.name),
        _escape_string(series.units),
        _escape_string(series.description),
        _counter_class_to_bm_str(series.counter_class),
        _counter_display_to_bm_str(series.get_display()),
        series.multiplier,
        colour_str,
        '&{}'.format(sample_fn_name) if series.sample else 'BM_NULL'
        ))

    # if the counter needs to be manually updated (i.e. it's not sampled) then generate
    # the update function implementations
    if not series.sample:
        update_fn = 'barman_cc_{}_{}_update_value'.format(chart.get_name_identifier(), series.get_name_identifier()).lower()
        update_now_fn = update_fn + '_now'
        per_core_param_decl = ', bm_uint32 core' if chart.per_cpu else ''
        per_core_param = 'core, ' if chart.per_cpu else '0, '
        get_core_param = ', barman_get_core_no()' if chart.per_cpu else ''

        lines.append('')
        lines.append('bm_bool {}(bm_uint64 timestamp{}, bm_uint64 value)'.format(update_fn, per_core_param_decl))
        lines.append('{')
        lines.append('    return barman_protocol_write_per_core_custom_counter(timestamp, {}'.format(per_core_param))
        lines.append('#if BM_CONFIG_MAX_TASK_INFOS > 0')
        lines.append('        barman_ext_get_current_task_id(),')
        lines.append('#endif')
        lines.append('        {}, value);'.format(series_index))
        lines.append('}')
        lines.append('')
        lines.append('bm_bool {}(bm_uint64 value)'.format(update_now_fn))
        lines.append('{')
        lines.append('    return {}(barman_ext_get_timestamp(){}, value);'.format(update_fn, get_core_param))
        lines.append('}')
        lines.append('')


def _generate_chart_definition(lines: List[str], all_charts_list: List[str], all_series_list: List[str], chart: CustomChart, chart_index: int):
    lines.append('/* ----------------- Custom counter chart {} ----------------- */'.format(chart.name))
    lines.append('')

    # keep a list of series in this chart so that we can define an array of them later
    series_for_this_chart: List[str] = []

    for i, series in enumerate(chart.series):
        constant_name = 'BM_CUSTOM_CHART_{}_SERIES_{}'.format(
            chart.get_name_identifier(), series.get_name_identifier()).upper()
        series_for_this_chart.append(constant_name)
        # also record this in the list of all series across all charts
        all_series_list.append(constant_name)

        # now write out the structures for the series
        _generate_series_definition(lines, chart, chart_index, series, i, constant_name)

    # generate the array of pointers to the series
    series_count = len(chart.series)
    series_array_constant = 'BM_CUSTOM_CHART_{}_SERIES_LIST'.format(chart.get_name_identifier().upper())
    lines.append('static const struct bm_custom_counter_chart_series * const {}[{}] = {{'.format(
        series_array_constant, series_count))

    for i, constant in enumerate(series_for_this_chart):
        if i < len(series_for_this_chart) - 1:
            lines.append('    &{},'.format(constant))
        else:
            lines.append('    &{}'.format(constant))
    lines.append('};')

    # now generate the constant for the chart
    chart_constant = 'BM_CUSTOM_CHART_{}'.format(chart.get_name_identifier().upper())
    series_comp = _series_composition_to_bm_str(chart.series_composition)
    render_type = _rendering_type_to_bm_str(chart.rendering_type)

    lines.append('static const struct bm_custom_counter_chart {} = {{"{}", {}, {}, {}, {}, {}, {}, {}, {}}};'.format(
        chart_constant,
        _escape_string(chart.name),
        series_comp,
        render_type,
        _bool_to_bm_str(chart.average_selection),
        _bool_to_bm_str(chart.average_cores),
        _bool_to_bm_str(chart.percentage),
        _bool_to_bm_str(chart.per_cpu),
        len(chart.series),
        series_array_constant))

    lines.append('')
    # record this chart in the list of all charts so far
    all_charts_list.append(chart_constant)


class CustomCounterTokenProcessor(TokenDirectiveProcessor):
    CUSTOM_COUNTERS_CONSTANTS_HEADER = "CUSTOM_COUNTERS_CONSTANTS_HEADER"
    CUSTOM_COUNTERS_CONSTANTS = "CUSTOM_COUNTERS_CONSTANTS"
    CUSTOM_COUNTERS_DEFINITIONS = "CUSTOM_COUNTERS_DEFINITIONS"

    def __init__(self, config: BarmanConfig):
        self._config = config

    def register_with_source_processor(self, processor: SourceProcessor):
        processor.register_token_processor(CustomCounterTokenProcessor.CUSTOM_COUNTERS_CONSTANTS_HEADER, self)
        processor.register_token_processor(CustomCounterTokenProcessor.CUSTOM_COUNTERS_CONSTANTS, self)
        processor.register_token_processor(CustomCounterTokenProcessor.CUSTOM_COUNTERS_DEFINITIONS, self)

    def replace_token(self, token: str) -> List[str]:
        if token == CustomCounterTokenProcessor.CUSTOM_COUNTERS_CONSTANTS_HEADER:
            return self._replace_custom_counters_constants_header()
        elif token == CustomCounterTokenProcessor.CUSTOM_COUNTERS_CONSTANTS:
            return self._replace_custom_counters_constants()
        elif token == CustomCounterTokenProcessor.CUSTOM_COUNTERS_DEFINITIONS:
            return self._replace_custom_counter_definitions()
        else:
            raise ValueError('Unsupported replacement token: {}'.format(token))

    def _replace_custom_counters_constants_header(self) -> List[str]:
        lines = []
        lines.append('/* ----------------- Custom counters ----------------- */')
        lines.append('')

        for chart in self._config.custom_charts:
            for series in chart.series:
                if series.sample:
                    _generate_sampled_series_declaration(lines, chart, series)
                else:
                    _generate_update_series_declaration(lines, chart, series)
        return lines


    def _replace_custom_counters_constants(self) -> List[str]:
        chart_count = len(self._config.custom_charts)
        series_count = reduce(lambda x, y: x+y,
            map(lambda x: len(x.series), self._config.custom_charts),
            0)

        lines = []
        lines.append('/* ----------------- Custom counters ----------------- */')
        lines.append('')
        lines.append('#define BM_NUM_CUSTOM_COUNTERS {}'.format(series_count))
        lines.append('#define BM_CUSTOM_CHARTS_COUNT {}'.format(chart_count))
        lines.append('')

        if series_count > 0:
            lines.append('extern const struct bm_custom_counter_chart * const BM_CUSTOM_CHARTS[BM_CUSTOM_CHARTS_COUNT];')
            lines.append('extern const struct bm_custom_counter_chart_series * const BM_CUSTOM_CHARTS_SERIES[BM_NUM_CUSTOM_COUNTERS];')
            lines.append('')

        return lines


    def _replace_custom_counter_definitions(self) -> List[str]:
        lines: List[str] = []

        all_chart_names: List[str] = []
        all_series_names: List[str] = []

        for i, chart in enumerate(self._config.custom_charts):
            _generate_chart_definition(lines, all_chart_names, all_series_names, chart, i)

        # if at least one series was found generate the top-level chart & series arrays
        if len(all_series_names) > 0:
            lines.append('/* ----------------- Custom counters ----------------- */')
            lines.append('')
            # generate the array of chart identifiers
            lines.append('const struct bm_custom_counter_chart * const BM_CUSTOM_CHARTS[BM_CUSTOM_CHARTS_COUNT] = {')
            for i, name in enumerate(all_chart_names):
                if i < len(all_chart_names) - 1:
                    lines.append('    &{},'.format(name))
                else:
                    lines.append('    &{}'.format(name))
            lines.append('};')

            # now generate the list of all series names across all charts
            lines.append('const struct bm_custom_counter_chart_series * const BM_CUSTOM_CHARTS_SERIES[BM_NUM_CUSTOM_COUNTERS] = {')
            for i, name in enumerate(all_series_names):
                if i < len(all_series_names) - 1:
                    lines.append('    &{},'.format(name))
                else:
                    lines.append('    &{}'.format(name))
            lines.append('};')

        return lines