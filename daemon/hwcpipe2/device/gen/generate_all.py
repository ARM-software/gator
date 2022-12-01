#!/usr/bin/env python3
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

"""
Generate code for all ioctl interfaces.

The script iterates over all possible combinations of templates
from `templates` and interfaces from `interfaces`, and feeds them
to `ioctl_idlc` generator.
"""

import asyncio
import sys
import itertools
import argparse

from typing import List, Iterable, overload
from pathlib import Path

from dataclasses import dataclass


@dataclass
class GeneratorError(Exception):
    """
    compile.py error.

    :param command: Generator command.
    :param stdout: Standard out stream of the generator process.
    :param stderr: Standard error stream of the generator process.
    """

    command: str
    stdout: bytes
    stderr: bytes

    def __str__(self):
        return "\n".join(
            [
                "Error while executing generation command!",
                "Command:",
                self.command,
                "stdout:",
                self.stdout.decode("utf-8"),
                "stderr:",
                self.stderr.decode("utf-8"),
            ]
        )


@overload
def script_path_to_full(filename: str) -> str:
    """
    Expand from script path to a full path.

    :param filename: Filename to expand.
    :return: Expanded full path.
    """


@overload
def script_path_to_full(filenames: Iterable[str]) -> List[str]:
    """
    Expand a list of script paths to a list of full paths.

    :param filename: List of filenames to expand.
    :return: Expanded list of full paths.
    """


def script_path_to_full(arg):
    """
    Common entry point for `script_path_to_full`.

    :param: Argument to process.
    :return: Full path or list of full paths.
    """
    if isinstance(arg, str):
        return str((Path(__file__).parent / arg).resolve())

    # Assume iterable
    return [script_path_to_full(item) for item in arg]


@dataclass
class TemplateArg:
    """
    Template and partials arguments to pass to compile.py

    :param outfile_fmt: Format string to generate output file name.
                        The following variables can be used:
                        `iface_name` - stem of the yaml file.
                        `template_stem` - stem of the template file.
    :param template: --template argument passed to compile.py.
    :param partials: --partials argument passed to compile.py.
    """

    outfile_fmt: str
    template: str
    partials: List[str]

    def __init__(self, template: str, partials: List[str], outfile_fmt: str):
        self.template = script_path_to_full(template)
        self.partials = script_path_to_full(partials)
        self.outfile_fmt = outfile_fmt


@dataclass
class YamlArg:
    "yaml argument to pass to compile.py"
    filename: str

    def __init__(self, filename: str):
        self.filename = script_path_to_full(filename)


@dataclass
class Options:
    """
    This script options.

    :param dry: Do not execute any commands, but print them.
    :param check_no_changes: If set, the script checks that the generated files
                             are up to date.
    """

    dry: bool
    check_no_changes: bool


def get_outfile_name(template_arg: TemplateArg, yaml_arg: YamlArg) -> str:
    """
    Generate output file name.

    :param template_arg: Template generator arguments.
    :param yaml_arg: Yaml generator argument.
    """
    iface_name = Path(yaml_arg.filename).stem
    template_stem = Path(template_arg.template).stem
    outfile_name = template_arg.outfile_fmt.format(
        iface_name=iface_name, template_stem=template_stem
    )

    return script_path_to_full(outfile_name)


async def exec_generator(
    options: Options, template_arg: TemplateArg, yaml_arg: YamlArg
):
    """
    Execute the generator.

    :param options: This script options.
    :param template_arg: Template generator arguments.
    :param yaml_arg: Yaml generator argument.
    """
    args = [yaml_arg.filename, "--template", template_arg.template]

    if template_arg.partials:
        args += ["--partials"] + template_arg.partials

    outfile_name = get_outfile_name(template_arg, yaml_arg)
    args += ["--out", outfile_name]

    if options.check_no_changes:
        args.append("--check-no-changes")

    program = script_path_to_full("ioctl_idlc/compile.py")
    command = [program] + args

    if options.dry:
        print(" ".join(command))
        return

    proc = await asyncio.create_subprocess_exec(
        program, *args, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE
    )

    stdout, stderr = await proc.communicate()

    if proc.returncode != 0:
        raise GeneratorError(
            command=" ".join(command),
            stdout=stdout,
            stderr=stderr,
        )

    print(stdout.decode("utf-8"), end="")


async def async_main(options: Options):
    """
    Async main entry point.

    :param options: This script options.
    """
    template_args = [
        TemplateArg(
            outfile_fmt="../src/device/ioctl/{iface_name}/{template_stem}",
            template="template/commands.hpp.mustache",
            partials=[
                "partial/description.mustache",
                "partial/copyright.mustache",
            ],
        ),
        TemplateArg(
            outfile_fmt="../src/device/ioctl/{iface_name}/{template_stem}",
            template="template/types.hpp.mustache",
            partials=[
                "partial/types.mustache",
                "partial/description.mustache",
                "partial/bitmask_operators.mustache",
                "partial/copyright.mustache",
            ],
        ),
        TemplateArg(
            outfile_fmt="../src/device/ioctl/{iface_name}/{template_stem}",
            template="template/print.hpp.mustache",
            partials=[
                "partial/print_declaration.mustache",
                "partial/copyright.mustache",
            ],
        ),
        TemplateArg(
            outfile_fmt="../src/device/ioctl/{iface_name}/{template_stem}",
            template="template/print.cpp.mustache",
            partials=[
                "partial/print.mustache",
                "partial/copyright.mustache",
            ],
        ),
        TemplateArg(
            outfile_fmt="../src/device/ioctl/{iface_name}/{template_stem}",
            template="template/compare.hpp.mustache",
            partials=[
                "partial/compare_declaration.mustache",
                "partial/copyright.mustache",
            ],
        ),
        TemplateArg(
            outfile_fmt="../src/device/ioctl/{iface_name}/{template_stem}",
            template="template/compare.cpp.mustache",
            partials=[
                "partial/compare.mustache",
                "partial/copyright.mustache",
            ],
        ),
        TemplateArg(
            outfile_fmt="../docs/ioctl/{iface_name}_{template_stem}",
            template="template/graph.dot.mustache",
            partials=[
                "partial/subgraph.mustache",
                "partial/subgraph_edges.mustache",
            ],
        ),
    ]

    yaml_args = [
        YamlArg("iface/kinstr_prfcnt.yaml"),
        YamlArg("iface/vinstr.yaml"),
        YamlArg("iface/kbase.yaml"),
        YamlArg("iface/kbase_pre_r21.yaml"),
    ]

    tasks = (
        exec_generator(options, template_arg, yaml_arg)
        for template_arg, yaml_arg in itertools.product(template_args, yaml_args)
    )

    await asyncio.gather(*tasks)


def main():
    """
    Main script entry point.

    :return: 0 on success, negative error code on failure.
    """
    parser = argparse.ArgumentParser(
        description="Generate all files from the ioctl interface descriptions."
    )

    parser.add_argument(
        "--dry",
        action="store_true",
        default=False,
        help="If set, generation commands are not executed, but printed.",
    )
    parser.add_argument(
        "--check-no-changes",
        action="store_true",
        default=False,
        help="Check that data rendered is the same as the output files.",
    )

    args = parser.parse_args()

    options = Options(args.dry, args.check_no_changes)

    asyncio.run(async_main(options))
    return 0


if __name__ == "__main__":
    sys.exit(main())
