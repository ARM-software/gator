#!/usr/bin/env/python3
#
# Copyright (c) 2022 ARM Limited.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

"The command line main entry point module."

from __future__ import annotations

import io
import sys
import argparse
from pathlib import Path
from typing import Optional, List, Iterable, Dict
from dataclasses import dataclass
import pprint

import yaml
import pystache

from .loader_ast import Loader


@dataclass
class Options:
    """
    Command line tool options.

    :param yaml_filename: yaml file to process.
    :param template_filename: Mustache template to process.
    :param partials: List of mustache partials to be used with the template.
    :param out: Output file name. None for writing to stdout.
    :param check_no_changes: If set, check that data rendered is the same as the output file.
    """

    yaml_filename: str
    template_filename: Optional[str]
    partials: List[str]
    out: Optional[str]
    check_no_changes: bool


def _read_file(filename: str) -> str:
    with io.open(filename, "r", encoding="UTF-8") as file:
        return str(file.read())


def _partials_dict(partials: Iterable[str]) -> Dict[str, str]:
    """
    Construct dict of mustache partials.

    :param partials: Mustache partial filenames.
    :return: Mapping from mustache partial name to its content.
    """
    names = (Path(partial).stem for partial in partials)
    contents = (_read_file(partial) for partial in partials)
    return dict(zip(names, contents))


def main(options: Options) -> int:
    """
    Main entry point.

    :param options: Command line options.
    :return 0:
    """
    with io.open(options.yaml_filename, "r", encoding="UTF-8") as yaml_file:
        iface = yaml.load(yaml_file, Loader)

    if not options.template_filename:
        pprint.pprint(iface)
        return 0

    with io.open(options.template_filename, "r", encoding="UTF-8") as template:
        renderer = pystache.Renderer(partials=_partials_dict(options.partials))
        content = renderer.render(template.read(), iface, missing_tags="strict")

    if not options.out:
        print(content)
        return 0

    if Path(options.out).is_file():
        with io.open(options.out, "r", encoding="UTF-8") as out:
            old_content = out.read()

        if old_content == content:
            print(f"No changes {options.out}")
            return 0

        if options.check_no_changes:
            print("Rendered data differs from the outfile.", file=sys.stderr)
            return -1

    with io.open(options.out, "w", encoding="UTF-8") as out:
        out.write(content)
        print(f"Generated {options.out}")

    return 0


def cli_main():
    """
    Command line tool entry point.
    """
    parser = argparse.ArgumentParser(description="ioctl interface compiler.")

    parser.add_argument("yaml", help="yaml file to parse.")

    parser.add_argument(
        "--template",
        default=None,
        help="Mustache template file to use for data rendering. "
        "If not set, parsed data will be print to stdout.",
    )
    parser.add_argument(
        "--partials",
        nargs="+",
        default=[],
        help="Mustache partials to use for data rendering.",
    )
    parser.add_argument(
        "--out", default="Output file for data rendered. If not set, print to stdout."
    )
    parser.add_argument(
        "--check-no-changes",
        action="store_true",
        default=False,
        help="Check that data rendered is the same as the output file.",
    )

    args = parser.parse_args()
    options = Options(
        args.yaml, args.template, args.partials, args.out, args.check_no_changes
    )

    sys.exit(main(options))
