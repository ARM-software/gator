#!/usr/bin/env python3
#
# Copyright (c) 2021-2022 ARM Limited.
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
Helper script to generate hwcpipe build for a certain target.
"""

import argparse
import itertools
import os
import subprocess
import sys

from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Optional, Iterable, List, Dict


class OsName(Enum):
    "Operating systems supported."
    LINUX = "linux"
    ANDROID = "android"


class Arch(Enum):
    "Target architectures supported."
    X86 = "x86"
    X64 = "x64"
    ARM = "arm"
    ARM64 = "arm64"


class Toolchain(Enum):
    "Compiler tool-chains supported."
    GCC = "gcc"
    CLANG = "clang"


class BuildType(Enum):
    "Build type."
    NONE = "none"
    DEBUG = "debug"
    RELEASE = "release"
    PROFILE = "profile"


_ARCH_TO_ANDROID_ARCH_ABI = {
    Arch.ARM: "armeabi-v7a",
    Arch.ARM64: "arm64-v8a",
}

_SOURCE_DIR = (Path(__file__).parent / "..").resolve()
_ANDROID_PLATFORM_VERSION = 24
_OUT_DIR = _SOURCE_DIR / "out"
_BUILD_DIR_FORMAT = "{out_dir}/{os}_{toolchain}_{build_type}_{arch}"


@dataclass(frozen=True)
class BuildInfo:
    """
    :param os: Target operating system.
    :param arch: Target architecture.
    :param toolchain: Compiler toolchain to use.
    :param build_type: Build type, e.g. release/debug.
    :param toolchain_version: Toolchain version.
    :param android_platform_version: Android platform version.
    """

    os_name: OsName
    arch: Arch
    toolchain: Toolchain
    build_type: BuildType
    toolchain_version: Optional[int]
    android_platform_version: Optional[int]

    def __str__(self):
        return "_".join(
            [
                f"{self.os_name.value}",
                f"{self.toolchain.value}",
                f"{self.build_type.value}",
                f"{self.arch.value}",
            ]
        )

    def is_supported(self) -> bool:
        """
        Check if this build configuration is supported.

        All combinations are supported except android/gcc and android/x86/x64.

        :return: True if configuration is supported, False otherwise.
        """
        if self.os_name == OsName.LINUX:
            return True

        assert self.os_name == OsName.ANDROID

        if self.toolchain == Toolchain.GCC:
            return False

        if self.arch not in _ARCH_TO_ANDROID_ARCH_ABI:
            return False

        return True

    def get_build_dir(self, args: argparse.Namespace) -> Path:
        """
        Extract build_dir from command line arguments or auto generate it if not provided.

        :param build_info: Target information, e.g. OS, arch and toolchain.
        :param arg:         Arguments parsed with argparse.
        """
        if "build_dir" in args:
            return Path(args.build_dir)

        build_dir_format = getattr(args, "build_dir_format", _BUILD_DIR_FORMAT)
        out_dir = getattr(args, "out_dir", _OUT_DIR)

        return Path(
            build_dir_format.format(
                out_dir=out_dir,
                os=self.os_name.value,
                toolchain=self.toolchain.value,
                arch=self.arch.value,
                build_type=self.build_type.value,
            )
        )

    @staticmethod
    def generate(args: argparse.Namespace) -> Iterable["BuildInfo"]:
        """
        Generate all possible configurations that agree with the arguments specified.

        :param args: Arguments to parse.
        :return: Build configurations generated that meet the requirements.
        """
        toolchain_version = getattr(args, "toolchain_version", None)
        apv = getattr(args, "android_platform_version", _ANDROID_PLATFORM_VERSION)

        build_types = [BuildType.DEBUG, BuildType.RELEASE]

        result = (
            BuildInfo(*options, toolchain_version, apv)
            for options in itertools.product(OsName, Arch, Toolchain, build_types)
        )

        result = (b for b in result if b.is_supported())

        if "os" in args:
            result = (b for b in result if b.os_name == OsName(args.os))
        if "arch" in args:
            result = (b for b in result if b.arch == Arch(args.arch))
        if "toolchain" in args:
            result = (b for b in result if b.toolchain == Toolchain(args.toolchain))
        if "build_type" in args:
            result = (b for b in result if b.build_type == BuildType(args.build_type))

        return result

    @staticmethod
    def parse(args: argparse.Namespace) -> "BuildInfo":
        """
        Parse build configuration from arguments.

        :return: Build configuration parsed.
        """
        default_arch_map = {
            OsName.LINUX: Arch.X64.value,
            OsName.ANDROID: Arch.ARM64.value,
        }
        default_toolchain_map = {
            OsName.LINUX: Toolchain.GCC.value,
            OsName.ANDROID: Toolchain.CLANG.value,
        }

        build_type_enum = BuildType(getattr(args, "build_type", "debug"))
        os_enum = OsName(getattr(args, "os", "linux"))
        arch_enum = Arch(getattr(args, "arch", default_arch_map[os_enum]))
        toolchain_enum = Toolchain(
            getattr(args, "toolchain", default_toolchain_map[os_enum])
        )
        toolchain_version = getattr(args, "toolchain_version", None)
        apv = getattr(args, "android_platform_version", _ANDROID_PLATFORM_VERSION)

        return BuildInfo(
            os_enum, arch_enum, toolchain_enum, build_type_enum, toolchain_version, apv
        )


class AndroidNdkHomeError(Exception):
    """Raised when ANDROID_NDK_HOME evn variable is not set."""

    def __init__(self):
        super().__init__(
            "$ANDROID_NDK_HOME must point to a directory containing an installed Android NDK!"
        )


@dataclass(frozen=True)
class Command:
    """
    Command line command to execute.

    :param what: Human readable command description.
    :param command: Command to execute.
    """

    what: str
    command: List[str]

    def execute(self, dry: bool) -> int:
        """
        Execute command.

        :param dry: If set, command is not executed but printed to stdout.
        :return: Command return code.
        """
        if dry:
            print("# " + self.what)
            print(" ".join(self.command))
            return 0

        process = subprocess.run(self.command, check=False)
        return process.returncode

    @staticmethod
    def generate(
        build_info: BuildInfo, build_dir: Path, cmake_generator: str, rest: List[str]
    ) -> "Command":
        """
        Create cmake command to generate build with parameters specified.

        :param build_info: Build configuration.
        :param build_dir: Build directory.
        :param cmake_generator: Which cmake generator to use.
        :param rest: Remaining arguments to pass to cmake command line.
        :return: Command that does cmake build generation.
        """
        cmake_command = [
            "cmake",
            "-S",
            str(_SOURCE_DIR),
            "-B",
            str(build_dir),
        ]

        if not build_info.is_supported():
            raise ValueError(f"{build_info} is not supported.")

        build_type2args: Dict[BuildType, List[str]] = {
            BuildType.NONE: [],
            BuildType.DEBUG: ["-DCMAKE_BUILD_TYPE=Debug"],
            BuildType.RELEASE: ["-DCMAKE_BUILD_TYPE=Release"],
            BuildType.PROFILE: ["-DCMAKE_BUILD_TYPE=RelWithDebInfo"],
        }

        if build_info.os_name == OsName.LINUX:
            toolchain_path = str(_SOURCE_DIR / "build" / "toolchain-linux.cmake")
            cmake_command += [
                f"-DHWCPIPE_TOOLCHAIN={build_info.toolchain.value}",
                f"-DHWCPIPE_TARGET_ARCH={build_info.arch.value}",
            ]
            if build_info.toolchain_version is not None:
                cmake_command.append(
                    f"-DHWCPIPE_TOOLCHAIN_VERSION={build_info.toolchain_version}"
                )
            cmake_command.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain_path}")
        else:
            ndk_home = os.environ.get("ANDROID_NDK_HOME", None)
            if ndk_home is None:
                raise AndroidNdkHomeError()

            cmake_command += [
                "-DCMAKE_SYSTEM_NAME=Android",
                f"-DCMAKE_SYSTEM_VERSION={build_info.android_platform_version}",
                f"-DCMAKE_ANDROID_ARCH_ABI={_ARCH_TO_ANDROID_ARCH_ABI[build_info.arch]}",
                f"-DCMAKE_ANDROID_NDK={ndk_home}",
            ]

        cmake_command += build_type2args[build_info.build_type]
        cmake_command += ["-G", cmake_generator]
        cmake_command += rest

        return Command(f"Generating build for {build_info}", cmake_command)

    @staticmethod
    def build(build_dir: Path, rest: List[str]) -> "Command":
        """
        Create command to run build in a build directory

        :param build_dir: Build directory.
        :param rest: Rest arguments to pass to `cmake --build`.
        :return: Command created.
        """
        cmake_command = ["cmake", "--build", f"{str(build_dir.resolve())}"] + rest

        return Command(f"Run build in {str(build_dir)}", cmake_command)

    @staticmethod
    def build_all(out_dir: Path, rest: List[str]) -> Iterable["Command"]:
        """
        Create commands to build all in output directory.

        :param out_dir: Output directory.
        :param rest: Rest arguments to pass to `cmake --build`.
        :return: Commands created.
        """
        if not out_dir.is_dir():
            return []

        return (
            Command.build(build_dir, rest)
            for build_dir in out_dir.iterdir()
            if build_dir.is_dir() and (build_dir / "CMakeCache.txt").is_file()
        )


def __choices(e_cls) -> List[str]:
    """
    :return: Enum values extracted for use in argparse.
    """
    return list(e.value for e in e_cls)


def __add_build_options(parser: argparse.ArgumentParser) -> None:
    """
    Add build options to an argument parser.

    :param parser: Parser to add arguments to.
    """
    group = parser.add_argument_group("Build options")
    group.add_argument(
        "--os",
        help="Target operating system.",
        choices=__choices(OsName),
    )
    group.add_argument("--arch", help="Target architecture.", choices=__choices(Arch))
    group.add_argument(
        "--toolchain",
        help="Which compiler toolchain to use.",
        choices=__choices(Toolchain),
    )
    group.add_argument(
        "--toolchain-version", help="Compiler version to use, e.g. '8' for 'gcc-8'."
    )
    group.add_argument("--build-type", help="Build type.", choices=__choices(BuildType))
    group.add_argument("--android-platform-version", help="Android platform version.")


def __add_build_dir_options(parser: argparse.ArgumentParser) -> None:
    """
    Add build dir options to an argument parser.

    :param parser: Parser to add arguments to.
    """
    group = parser.add_argument_group("Build directory group")
    group.add_argument(
        "--build-dir-format",
        help="Python format string used to generate build directory name.\n"
        "The following variables are allowed:\n"
        "     out_dir: Output directory.\n"
        "          os: Target operating system.\n"
        "        arch: Target architecture.\n"
        "   toolchain: Compiler toolchain used.\n"
        "  build_type: Build type, e.g. debug/release.\n",
    )
    group.add_argument(
        "--out-dir",
        help="Additional variable for use in --build-dir-format argument.\n"
        "It is intended for specifying the root where all generated builds will\n"
        "be placed. By default it is set to '$REPO_ROOT/out'.",
    )
    group.add_argument(
        "--build-dir",
        help="Build directory. When specified, build directory name generation\n"
        "mechanism is not used. Instead, this argument specifies the build directory.\n"
        "Therefore, '--build-dir' is not compatible with '--all', '--out-dir' and\n"
        "'--build-dir-format' arguments.",
    )


def __check(parser: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    """
    Extra checks to make sure that arguments do not conflict.

    :param parser: Parser to use for errors raising, if any.
    :param args: Parsed arguments to check.
    """

    incompatible_attrs = itertools.product(
        ["build_dir"], ["build_dir_format", "out_dir", "all"]
    )
    for attr1, attr2 in incompatible_attrs:
        if attr1 in args and attr2 in args:
            opt1 = "--" + attr1.replace("_", "-")
            opt2 = "--" + attr2.replace("_", "-")
            parser.error(f"{opt1} is not allowed with {opt2}.")

    if args.rest:
        if args.rest[0] != "--":
            parser.error("Remaining arguments should be separated with a '--'.")
        args.rest = args.rest[1:]


def main() -> int:
    """
    Main script entry
    :return: 0 on success, 1 on failure.
    """
    parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)
    subparsers = parser.add_subparsers(
        dest="command", required=True, help="Type of cmake command to execute."
    )

    parser_generate = subparsers.add_parser(
        "generate",
        help="Generate cmake build with options specified.",
        formatter_class=argparse.RawTextHelpFormatter,
        argument_default=argparse.SUPPRESS,
    )

    __add_build_options(parser_generate)
    __add_build_dir_options(parser_generate)

    parser_generate.add_argument(
        "--generator",
        help="CMake generator to use. See 'cmake -G' for details.",
        default="Ninja",
    )
    parser_generate.add_argument(
        "--all",
        help="Generate builds for all targets supported.\n"
        "The generated builds will be stored under the output directory,\n"
        "with the directory names generated according to `--build-dir-format`\n"
        "argument. Cannot be used with `--build-dir` argument.",
        action="store_true",
    )

    parser_build_all = subparsers.add_parser(
        "build-all",
        help="Build everything found in the out_dir.",
        formatter_class=argparse.RawTextHelpFormatter,
        argument_default=argparse.SUPPRESS,
    )
    parser_build_all.add_argument(
        "--out-dir", help="Directory to scan for builds to execute."
    )

    for prs in (parser_generate, parser_build_all):
        prs.add_argument(
            "--dry",
            help="Don not execute any commands, but print them.",
            action="store_true",
        )
        prs.add_argument(
            "rest",
            help="Extra arguments to pass to the cmake command.\n"
            "The arguments must be separated from the script arguments\n"
            "with a '--'. Note, that if you want to pass arguments to\n"
            "the native build tool using 'build-all' command, you'll have\n"
            "to put '--' twice, for example:\n"
            "./cmake.py build-all -- -- -j32",
            nargs=argparse.REMAINDER,
        )

    commands = []
    args = parser.parse_args()

    __check(parser, args)

    if args.command == "generate":
        generator = getattr(args, "generator", "Ninja")
        if "all" in args:
            for build_info in BuildInfo.generate(args):
                build_dir = build_info.get_build_dir(args)
                commands.append(
                    Command.generate(build_info, build_dir, generator, args.rest)
                )
        else:
            build_info = BuildInfo.parse(args)
            build_dir = build_info.get_build_dir(args)
            commands.append(
                Command.generate(build_info, build_dir, generator, args.rest)
            )
    elif args.command == "build-all":
        out_dir = Path(getattr(args, "out_dir", _OUT_DIR))
        commands += Command.build_all(out_dir, args.rest)

    if not commands:
        return 0

    return max((c.execute("dry" in args) for c in commands))


if __name__ == "__main__":
    sys.exit(main())
