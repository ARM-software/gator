# Copyright (C) 2023 by Arm Limited
#
# SPDX-License-Identifier: BSD-3-Clause

from typing import Any, List, Optional, Callable

from xml.etree import ElementTree
from .config import *

class BadBarmanXmlError(Exception):
    def __init__(self, message):
        super().__init__(message)


class BarmanXmlParser:
    def __init__(self):
        self._config = BarmanConfig()
        self._seen_chart_names: Set[str] = set()


    def parse_from_str(self, contents: str) -> BarmanConfig:
        root = ElementTree.fromstring(contents)
        return self._parse_root_node(root)


    def parse(self, filename) -> BarmanConfig:
        tree = ElementTree.parse(filename)
        return self._parse_root_node(tree.getroot())


    def _parse_root_node(self, root) -> BarmanConfig:
        version = root.get('version')
        if version != '1':
            raise BadBarmanXmlError('Version {} is unsupported'.format(version))

        for child in list(root):
            if child.tag == 'runtime-config-defaults':
                self._config.runtime_config_defaults = self._parse_runtime_config(child)
            elif child.tag == 'data-store':
                self._config.data_storage_backend = self._parse_data_store(child)
            elif child.tag == 'stm-storage-backend-config':
                self._config.stm_config = self._parse_stm_config(child)
            elif child.tag == 'dwt-config':
                self._config.dwt_sampling_period = self._parse_dwt_config(child)
            elif child.tag == 'itm-config':
                self._config.itm_config = self._parse_itm_config(child)
            elif child.tag == 'streaming-config':
                self._config.streaming_header_every_n_records = self._parse_streaming_config(child)
            elif child.tag == 'processors':
                self._config.processor_configs = self._parse_processors(child)
            elif child.tag == 'target-name':
                self._config.target_name = child.text
            elif child.tag == 'clock-info':
                self._config.clock_info = self._parse_clock_info(child)
            elif child.tag == 'custom-charts':
                self._config.custom_charts = self._parse_custom_charts(child)

        return self._config


    def _parse_elem_bool(self, element: ElementTree.Element) -> bool:
        value = self._xml_str_to_bool(element.text)
        if value is None:
            raise BadBarmanXmlError('Tag {} contains invalid content. Expected boolean but found {}'.format(element.tag, element.text))
        return value


    def _xml_str_to_bool(self, value: str) -> Optional[bool]:
        if value.lower() in ['true', '1']:
            return True
        if value.lower() in ['false', '0']:
            return False
        return None


    def _parse_xml_number(self, element):
        try:
            return int(element.text, 0)
        except ValueError:
            raise BadBarmanXmlError('Value {} is not a valid integer'.format(element.text))


    def _get_attribute(self, element: ElementTree.Element, name: str, map_fn: Callable[[str], Any], default) -> Any:
        attr = element.get(name)
        if attr is not None:
            return map_fn(attr)
        return default


    def _get_str_attribute(self, element: ElementTree.Element, name: str, default: Optional[str] = None) -> Optional[str]:
        return self._get_attribute(element, name, lambda x: x, default)


    def _get_bool_attribute(self, element: ElementTree.Element, name: str, default: Optional[bool] = None) -> Optional[bool]:
        return self._get_attribute(element, name, self._xml_str_to_bool, default)


    def _get_int_attribute(self, element: ElementTree.Element, name: str, default: Optional[int] = None) -> Optional[int]:
        parse_fn = lambda x: int(x, 0)
        return self._get_attribute(element, name, parse_fn, default)


    def _get_float_attribute(self, element: ElementTree.Element, name: str, default: Optional[float] = None) -> Optional[float]:
        parse_fn = lambda x: float(x)
        return self._get_attribute(element, name, parse_fn, default)


    def _parse_runtime_config(self, element):
        config = RuntimeConfigDefaults()

        for child in list(element):
            if child.tag == 'use-builtin-memfuncs':
                config.enable_builtin_mem_funcs = self._parse_elem_bool(child)
            elif child.tag == 'enable-debug-logging':
                config.enable_debug_logging = self._parse_elem_bool(child)
            elif child.tag == 'enable-logging':
                config.enable_logging = self._parse_elem_bool(child)
            elif child.tag == 'max-processors':
                config.max_cores = self._parse_xml_number(child)
            elif child.tag == 'max-mmap-layout-entries':
                config.max_mmap_layout_entries = self._parse_xml_number(child)
            elif child.tag == 'max-task-entries':
                config.max_task_entries = self._parse_xml_number(child)
            elif child.tag == 'min-sample-period':
                config.minimum_sample_period = self._parse_xml_number(child)
            else:
                raise BadBarmanXmlError('Unknown tag [{}] in runtime-config-defaults.'.format(child.tag))

        return config


    def _parse_data_store(self, elem) -> DataStorageBackend:
        try:
            return DataStorageBackend(elem.text)
        except ValueError:
            raise BadBarmanXmlError('Unsupported data-store backend: {}'.format(elem.text))


    def _parse_stm_config(self, elem) -> StmBackendConfig:
        config = StmBackendConfig()

        for child in list(elem):
            if child.tag == 'master-ids':
                ids: List[int] = []
                for id_child in list(child):
                    if id_child.tag == 'value':
                        ids.append(self._parse_xml_number(id_child))
                    else:
                        raise BadBarmanXmlError('Unsupported element [{}] inside master-ids'.format(id_child.tag))
                config.master_ids = ids

            elif child.tag == 'starting-channel-number':
                config.min_channel_number = self._parse_xml_number(child)
            elif child.tag == 'number-of-channels':
                config.number_of_channels = self._parse_xml_number(child)
            else:
                raise BadBarmanXmlError('Unsupported STM config element [{}]'.format(child.tag))

        return config


    def _parse_dwt_config(self, elem) -> int:
        sampling_period = 0

        for child in list(elem):
            if child.tag == 'pc-sampling-period':
                sampling_period = self._parse_xml_number(child)
            else:
                raise BadBarmanXmlError('Unsupported element [{}] inside dwt-config'.format(child.tag))

        return sampling_period


    def _parse_itm_config(self, elem) -> ItmBackendConfig:
        config = ItmBackendConfig()

        for child in list(elem):
            if child.tag == 'port-number':
                config.port_number = self._parse_xml_number(child)
            # this tag is misspelled in Streamline's config generator so support both spellings
            elif child.tag == 'occupied-port-count' or child.tag == 'occuppied-port-count':
                config.num_ports_used = self._parse_xml_number(child)
            else:
                raise BadBarmanXmlError('Unsupported element [{}] inside itm-config'.format(child.tag))

        return config

    def _parse_streaming_config(self, elem) -> int:
        header_repeat = 500

        for child in list(elem):
            if child.tag == 'header-every-n-records':
                header_repeat = self._parse_xml_number(child)
            else:
                raise BadBarmanXmlError('Unsupported element [{}] inside streaming-config'.format(child.tag))

        return header_repeat

    def _parse_processors(self, elem) -> List[ProcessorConfig]:
        if self._config.processor_configs is not None:
            raise BadBarmanXmlError("Multiple 'processors' tags found but only 1 was expected.")

        configs = []

        for child in list(elem):
            if child.tag == 'processor':
                configs.append(self._parse_processor(child))
            else:
                raise BadBarmanXmlError('Unsupported element [{}] inside processors'.format(child.tag))

        return configs


    def _parse_processor(self, elem: ElementTree.Element) -> ProcessorConfig:
        processor = ProcessorConfig()
        processor.sample_cycle_counter = elem.get("cycle-counter", False)
        processor.name = elem.get('name')
        processor.cpuid = int(elem.get('cpuid', '0'), 0)

        if processor.name is None:
            raise BadBarmanXmlError('Processor tag must have a "name" attribute')

        if processor.cpuid == 0:
            raise BadBarmanXmlError('Processor tag must have a "cpuid" attribute')

        for child in list(elem):
            if child.tag == 'event':
                event_type = child.get("type")
                if event_type is None:
                    raise BadBarmanXmlError('Processor event tag is missing the required "type" attribute.')

                try:
                    processor.events.add(int(event_type, 0))
                except ValueError:
                    raise BadBarmanXmlError('Value [{}] is not a valid processor event type integer'.format(event_type))

        return processor


    def _parse_clock_info(self, elem: ElementTree.Element) -> ClockInfo:
        info = ClockInfo()

        for child in list(elem):
            if child.tag == 'timestamp-base':
                info.timestamp_base = self._parse_xml_number(child)
            elif child.tag == 'timestamp-divisor':
                info.timestamp_divisor = self._parse_xml_number(child)
            elif child.tag == 'timestamp-multiplier':
                info.timestamp_multiplier = self._parse_xml_number(child)
            elif child.tag == 'unix-base':
                info.unix_base_ns = self._parse_xml_number(child)
            else:
                raise BadBarmanXmlError('Unsupported element [{}] inside clock-info'.format(child.tag))

        return info


    def _parse_custom_charts(self, elem: ElementTree.Element) -> List[CustomChart]:
        results: List[CustomChart] = []

        for child in list(elem):
            if child.tag == 'chart':
                chart = self._parse_custom_chart(child)
                if chart is not None:
                    results.append(chart)
            else:
                raise BadBarmanXmlError('Unsupported element [{}] inside custom-charts'.format(child.tag))

        return results


    def _parse_custom_chart(self, elem: ElementTree.Element) -> CustomChart:
        chart = CustomChart()
        chart.name = elem.get('name')
        if chart.name is None:
            raise BadBarmanXmlError('Missing "name" attribute on custom "chart" element.')

        if chart.name in self._seen_chart_names:
            raise BadBarmanXmlError('Duplicate custom chart name detected: "{}"'.format(chart.name))
        self._seen_chart_names.add(chart.name)

        series_composition = elem.get('series_composition')
        if series_composition is not None:
            chart.series_composition = SeriesComposition.from_string(series_composition)

        rendering_type = elem.get('rendering_type')
        if rendering_type is not None:
            chart.rendering_type = RenderingType.from_string(rendering_type)

        chart.average_selection = self._get_bool_attribute(elem, 'average_selection', default=chart.average_selection)
        chart.average_cores = self._get_bool_attribute(elem, 'average_cores', default=chart.average_cores)
        chart.percentage = self._get_bool_attribute(elem, 'percentage', default=chart.percentage)
        chart.per_cpu = self._get_bool_attribute(elem, 'per_cpu', default=chart.per_cpu)

        seen_series_names: List[str] = []
        for child in list(elem):
            if child.tag == 'series':
                chart.series.append(self._parse_custom_chart_series(child, seen_series_names))
            else:
                raise BadBarmanXmlError('Unsupported element [{}] inside chart'.format(child.tag))
        return chart


    def _parse_custom_chart_series(self, elem: ElementTree.Element, already_seen_names: List[str]) -> CustomChartSeries:
        series = CustomChartSeries()
        series.name = elem.get('name')
        if series.name is None:
            raise BadBarmanXmlError('Missing "name" attribute on "series" element. All custom chart series must have a name')
        if series.name in already_seen_names:
            raise BadBarmanXmlError('Detected a duplicate counter series name "{}".'.format(series.name))
        already_seen_names.append(series.name)

        series.units = elem.get('units')
        if series.units is None:
            raise BadBarmanXmlError('Missing "units" attribute from custom chart "series" element.')

        series.sample = self._get_bool_attribute(elem, 'sample', default=series.sample)

        counter_class = elem.get('class')
        if counter_class is not None:
            series.counter_class = CounterClass.from_string(counter_class)

        counter_display = elem.get('display')
        if counter_display is not None:
            series.set_display(CounterDisplay.from_string(counter_display))

        if not series.get_display().check_compatible_with(series.counter_class):
            raise BadBarmanXmlError('Custom counter series has display type "{}" which is not compatible with counter class "{}".'
                .format(series.get_display().name, series.counter_class.name))

        series.multiplier = self._get_float_attribute(elem, 'multiplier', default=series.multiplier)
        series.colour = self._get_int_attribute(elem, 'colour')
        series.description = self._get_str_attribute(elem, 'description', default=series.description)

        return series