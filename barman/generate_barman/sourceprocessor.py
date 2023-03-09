# Copyright (C) 2023 by Arm Limited
#
# SPDX-License-Identifier: BSD-3-Clause

from typing import Dict, List, Set
import os.path
import re

COPYRIGHT_TEXT = """/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2023 by Arm Limited */
"""

class ProcessingError(Exception):
    def __init__(self, message):
        super().__init__(message)


class FileContentResolver:
    """
    This class is responsible for locating files amongst a user-defined set of
    directories and fetching their content. It maintains a list of files that
    have already been seen so that file content can be included only once.
    """

    def __init__(self, search_paths: List[str]):
        self._search_paths = search_paths
        self._seen_files: Set[str] = set()
        self._override_content: Dict[str, List[str]] = {}


    def register_content(self, name: str, content: str):
        """Specify some content to be returned for a given file.

        When this instance is asked to provide content from the file called
        [name] the provided [content] will be returned, instead of reading from
        the filesystem.
        """
        self._override_content[name] = content.splitlines()


    def require_once(self, name: str) -> List[str]:
        """Fetch the content from a file only once.

        Locates a file in the list of search directories (or the manually
        specified override content) and returns the file's lines as a list of
        strings. The content will only be returned once: further calls to this
        function for the same filename will return an empty list.
        """
        if name in self._seen_files:
            return []

        self._seen_files.add(name)

        if name in self._override_content:
            return self._override_content[name]

        path = self._search_for_file(name)
        with open(path, 'r') as f:
            return f.read().splitlines()


    def _search_for_file(self, resource: str):
        for path in self._search_paths:
            test_path = os.path.join(path, resource)
            if (os.path.exists(test_path)):
                return test_path
        raise ProcessingError('File "{}" could not be found.'.format(resource))


class TokenDirectiveProcessor:
    """
    Token Processors watch out for pre-defined strings within the source files
    and replace those strings with zero or more other lines. This is used to
    generate lines of source based on the options specified in the barman XML
    config.
    """
    def replace_token(self, token: str) -> List[str]:
        return []


class StrippingLineSink:
    """
    An output stream wrapper that prevents certain patterns of lines from being
    emitted into the output. Used to strip excess comments from the generated
    source.
    """
    def __init__(self, io_delegate):
        self._io_delegate = io_delegate
        self._last_line_was_blank = False
        self._strip_patterns = [
            # Include guard #ifndef pattern
            re.compile(r"^\s*#\s*ifndef\s*(INCLUDE_BARMAN[0-9A-Z_]*)(\s+.*)?$"),
            # Include guard #define pattern
            re.compile(r"^\s*#\s*define\s*(INCLUDE_BARMAN[0-9A-Z_]*)(\s+.*)?$"),
            # Include guard #endif pattern
            re.compile(r"^\s*#\s*endif\s*/\*\s*(INCLUDE_BARMAN[0-9A-Z_]*)(\s+.*)?\*/(\s+.*)?$"),
            # Copyright comments
            re.compile(r"^/\* Copyright \([Cc]\) [0-9]{4}(-[0-9]{4})? by Arm Limited.( All rights reserved\.)? \*/$"),
            # Licence comments
            re.compile(r"^\s*(//|/\*)\s*SPDX-License.*$"),
            # Doxygen @file comments
            re.compile(r"^/\*\*\s*@file(\s+.+)?\s*\*/$"),
            # system header pragmas
            re.compile(r'^\s*#\s*pragma\s+(GCC|clang)\s+system_header\s*.*$')
            ]

    def writeln_unfiltered(self, line:str):
        """Writes a line to the output stream ignoring any of the exclusion patterns."""
        self._io_delegate.write(line)
        self._io_delegate.write('\n')

    def writeln(self, line: str):
        for pattern in self._strip_patterns:
            if pattern.match(line) is not None: return

        # coalesce blank lines
        if line is None or len(line) == 0:
            if self._last_line_was_blank:
                return
            self._last_line_was_blank = True
        else:
            self._last_line_was_blank = False

        self._io_delegate.write(line);
        self._io_delegate.write('\n')


class SourceProcessor:
    """
    A Source Processor iterates through lines of source code, watching out for
    specific line patterns.

    #define patterns are optionally replaced with values
    taken from the barman config XML.

    #include directives are processed to insert other files inline within the
    generated source.

    Lines that look like /*#SOME_TOKEN*/ are tokens that should be passed to a
    TokenProcessor for replacement.
    """

    def __init__(self, content_resolver: FileContentResolver):
        self._define_pattern = re.compile(r"^\s*#\s*define\s*(\w+)\s*(.*)$")
        self._include_pattern = re.compile(r"^\s*#\s*include\s*((?:\"[^\"]+\")|(?:<[^>]+>))(.*)$")
        self._token_pattern = re.compile(r"/\*#([0-9A-Za-z_]+)\*/")
        self._content_resolver = content_resolver
        self._definitions: Dict[str, str] =  {}
        self._token_processors: Dict[str, TokenDirectiveProcessor] = {}
        # TODO: find a better way of doing this
        self._ignored_includes = ['barman.h']


    def register_token_processor(self, token: str, processor: TokenDirectiveProcessor):
        self._token_processors[token] = processor


    def register_definition(self, define: str, value):
        if isinstance(value, bool):
            self._definitions[define] = '1' if value else '0'
        else:
            self._definitions[define] = str(value)

    def register_definition_string(self, define: str, value: str):
        self._definitions[define] = '"{}"'.format(value)


    def process_file(self, filename: str, out_stream):
        """
        Iterates through the lines in the file, processing any directives and
        writing the results into the output stream.
        """
        sink = StrippingLineSink(out_stream)
        self._write_copyright_heading(sink)
        self._process_lines(self._content_resolver.require_once(filename), sink)


    def _write_copyright_heading(self, sink):
        sink.writeln_unfiltered(COPYRIGHT_TEXT)


    def _process_lines(self, lines: List[str], sink):
        for line in lines:
            if '#' in line:
                self._process_directive(line, sink)
            else:
                sink.writeln(line)


    def _process_directive(self, line: str, sink):
        match = self._include_pattern.match(line)
        if match is not None:
            self._process_include_line(line, match, sink)
            return

        match = self._define_pattern.match(line)
        if match is not None:
            self._process_define_line(line, match, sink)
            return

        match = self._token_pattern.match(line)
        if match is not None:
            self._process_token_replace(match, sink)
            return

        sink.writeln(line)


    def _process_include_line(self, line: str, match: re.match, sink):
        name = match.group(1)[1:-1]
        if name in self._ignored_includes:
            sink.writeln(line)
        else:
            content = self._content_resolver.require_once(name)
            self._process_lines(content, sink)


    def _process_define_line(self, line: str, match: re.match, sink):
        name = match.group(1)
        if name in self._definitions:
            replaced = line[0:match.start(2)] + self._definitions[name]
            sink.writeln(replaced)
        else:
            sink.writeln(line)


    def _process_token_replace(self, match: re.match, out_stream):
        token = match.group(1)
        if not token in self._token_processors:
            raise ProcessingError('Unrecognised token "{}" in template file'.format(token))
        content = self._token_processors[token].replace_token(token)
        self._process_lines(content, out_stream)