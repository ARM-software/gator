#!/usr/bin/env python3
# Copyright (C) 2019-2023 by Arm Limited
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
# TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""
The `streamline_me.py` script helps set up either an interactive Streamline
capture, or a headless gatord capture, for a single debuggable package running
on a non-rooted Android device. This script requires Python 3.5 or higher.


Prerequisites
=============

The test application APK must be debuggable and pre-installed on to the
target device prior to starting this script.

By default the script will look for the `gatord` binary to use in the current
working directory on the host. The following instructions will assume that
this is the case, but a user can specify a different path using the `--daemon`
command line option.

Headless captures need a capture configuration; by default the script will look
for a `configuration.xml` configuration file in the current working directory
on the host. The following instructions will assume that this is the case, but
a user can specify a different path using the `--config` command line option.
Note that interactive captures can optionally also specify the `--config`
option to use a pre-determined set of counters, but this is optional as
interactive use will prompt for a configuration via the Streamline GUI.


Interactive capture
===================

Interactive captures set up gatord on the device, but use the Streamline GUI
for configuring and capturing data. For a simple interactive captures run the
following command line:

    python streamline_me.py --package <name>

The on-screen prompts will inform you when it is safe to switch to the
Streamline GUI and capture your test data.

Once you have finished the data capture in Streamline return to the console
running the script and press the <Enter> key to clean up the device and
complete the test run.

If you do not know your package name you can run without the `--package`
option. In this case the script will search your device for debuggable packages
and prompt you to select one. The search can be quite slow, so it is
recommended to use the `--package` option once you know the package name.


Headless capture
================

Headless captures setup gatord on the device, and use gatord standalone to
capture data without the Streamline GUI connected. For a simple headless
capture run the following command line:

    python streamline_me.py --package <name> --headless <output>

This will use the `configuration.xml` in the current working directory to
perform a headless capture and store the results back to the output location on
the host. Outputs may be either a directory of the format <name.apc> or a file
of the format <name.apc.zip>. The path leading up to the output location must
exist already; the script will exit with an error if it doesn't.

By default headless captures stop when the test application exits on the
target. Note that the application must exit, not suspend. Alternatively
a fixed timeout can be specified using the `--headless-timeout` option,
which will capture that many seconds of data before exiting.

Headless captures will throw an error if the output location already exists.
To avoid this you can run with the `--overwrite` option, which will forcefully
delete the existing location and replace it with new data.

The output file can be imported into the Streamline GUI via two methods:

* If you wrote an ".apc" directory you can drag the directory from the host
  OS file explorer into the Streamline Data View capture list.
* If you wrote an ".apc.zip" file you can either drag the file from the host
  OS file explorer into the Streamline Data View capture list, or you can
  import the file using the import button in the Data View.


Hosts with multiple devices connected
=====================================

For host machines with a single debuggable device connected, this script will
auto-detect and use that device. If you have a machine with multiple devices
connected, for example a continuous integration server, you must either select
the device interactively from the menu or specify the device name on the
command line. For convenience, the command line may be a partial prefix of the
full name, provided that the prefix uniquely identifies a single device.
"""

# Early imports for a basic Python 2.7 compat check
from __future__ import print_function

import sys

try:
    import argparse as ap
    import atexit
    import datetime
    import math
    import os
    import re
    import shlex
    import shutil
    import subprocess as sp
    import tarfile
    import tempfile
    import textwrap

# Standard library import failure implies old Python
except ImportError:
    print("ERROR: Script requires Python 3.5 or newer")
    sys.exit(1)

# We know we have an API break with Python 3.4 or older
if (sys.version_info[0] < 3) or \
   (sys.version_info[0] == 3 and sys.version_info[1] < 5):
    print("ERROR: Script requires Python 3.5 or newer")
    sys.exit(1)


DEBUG_GATORD = False

# Android temp directory
ANDROID_TMP_DIR = "/data/local/tmp/"

# The minimum version Arm officially supports for this script
ANDROID_MIN_SUPPORTED_VERSION = 29

# OpenGL ES needs SDK version 29 (Android 10) for layers
ANDROID_MIN_OPENGLES_SDK = 29

# Vulkan needs SDK version 28 (Android 9) for layers
ANDROID_MIN_VULKAN_SDK = 28


# Performance Advisor configuration file name
CONFIG_FILE = "pa_lwi.conf"

# Maximum log line length
LINE_LENGTH = 80

# Expected layer names
EXPECTED_VULKAN_LAYER_NAME = "VK_LAYER_ARM_LWI"
EXPECTED_VULKAN_LAYER_FILE = "libVkLayerLWI.so"
EXPECTED_GLES_LAYER_FILE_NAME = "libGLESLayerLWI.so"
EXPECTED_VALIDATION_LAYER_NAME = "VK_LAYER_KHRONOS_validation"


class ArgFormatter(ap.HelpFormatter):
    """
    Custom argparse formatter to allow newlines in option text.
    """

    PREFIX = 'AF@'

    def _split_lines(self, text, width):

        # Use our custom formatter if enabled
        if text.startswith(self.PREFIX):
            return text[len(self.PREFIX):].splitlines()

        # Fall back to the default argparse formatter otherwise
        return ap.HelpFormatter._split_lines(self, text, width)


class Device:
    """
    A basic wrapper around adb, allowing a specific device to be registered.

    Attributes:
        device: The name of the device to call, or None for non-specific use.
    """

    def __init__(self, deviceName=None):
        """
        Create a new device, defaulting to non-specific use.

        Args:
            deviceName: The device identifier, as returned by "adb devices",
                or None for non-specific use.
        """
        self.device = deviceName

    def adb_async(self, *args):
        """
        Call `adb` to start a command, but do not wait for it to complete.

        Args:
            args: List of command line paramaters.

        Returns:
            The process instance.
        """
        # Run gatord
        commands = ["adb"]
        if self.device:
            commands.extend(["-s", self.device])
        commands.extend(args)

        # Note do not use shell=True; arguments are not safely escaped
        # Sink inputs to DEVNULL to stop the child process stealing keyboard
        # Sink outputs to DEVNULL to stop full output buffers blocking child
        if DEBUG_GATORD:
            stde = sys.stderr
            process = sp.Popen(commands, universal_newlines=True,
                               stdin=stde, stdout=stde)
        else:
            devn = sp.DEVNULL
            process = sp.Popen(commands, stdin=devn, stdout=devn, stderr=devn)

        return process

    def adb_quiet(self, *args):
        """
        Call `adb` to run a command, but ignore output and errors.

        Args:
            *args : List of command line paramaters.
        """
        commands = ["adb"]
        if self.device:
            commands.extend(["-s", self.device])
        commands.extend(args)

        # Note do not use shell=True; arguments are not safely escaped
        ret = sp.run(commands, stdout=sp.DEVNULL, stderr=sp.DEVNULL)

    def adb(self, *args, **kwargs):
        """
        Call `adb` to run command, and capture output and results.

        Args:
            *args: List of command line paramaters.
            **kwargs: Text: Is output is text, or binary? Shell: Use a shell?

        Returns:
            The contents of stdout.

        Raises:
            CalledProcessError: The subprocess was not successfully executed.
        """
        commands = ["adb"]
        if self.device:
            commands.extend(["-s", self.device])
        commands.extend(args)

        text = kwargs.get("text", True)
        shell = kwargs.get("shell", False)

        if shell:
            # Unix shells need a flattened command for shell commands
            if os.name != 'nt':
                quotedCommands = []
                for command in commands:
                    if command != ">":
                        command = shlex.quote(command)
                    quotedCommands.append(command)
                commands = " ".join(quotedCommands)

        rep = sp.run(commands, check=True, shell=shell, stdout=sp.PIPE,
                    stderr=sp.PIPE, universal_newlines=text)

        return rep.stdout


def select_from_menu(title, menuEntries):
    """
    Prompt user to select from an on-screen menu.

    If the option list contains only a single option it will be auto-selected.

    Args:
        title: The title string.
        menuEntries: The list of options.

    Returns:
        The selected list index, or None if no selection made.
    """
    assert len(menuEntries) > 0 # The caller is responsible for handling this case

    if len(menuEntries) == 1:
        print("\nSelect a %s:" % title)
        print("    Auto-selected %s" % menuEntries[0])
        return 0

    selection = None
    while True:
        try:
            # Print the menu
            print("\nSelect a %s:" % title)
            countW = int(math.log10(len(menuEntries))) + 1
            message = "    %%%uu) %%s" % countW
            for i, entry in enumerate(menuEntries):
                print(message % (i + 1, entry))
            print(message % (0, "Exit script"))

            # Process the response
            response = int(input("\n    Select entry: "))
            if response == 0:
                return None
            elif 0 < response <= len(menuEntries):
                selection = response - 1
                break
            else:
                raise ValueError()

        except ValueError:
            print("    Please enter an int in range 0-%u" % len(menuEntries))

    print("\n    Selected %s" % menuEntries[selection])
    return selection


def get_android_version(device):
    """
    Get the Android version of a given device.

    Args:
        device: The device instance.

    Returns:
        The Android version.
    """
    ver = device.adb("shell", "getprop", "ro.build.version.sdk")
    return float(ver)


def get_device_model(device):
    """
    Get the model of a given device

    Args:
        device: The device instance.

    Returns:
        The device model or None if the call failed.
    """
    try:
        logFile = device.adb("shell", "getprop", "ro.product.model")
        return logFile.strip()
    except sp.CalledProcessError:
        return None


def is_package_32bit_abi(device, package):
    """
    Get the target ABI of a given package.

    Args:
        device: The device instance.
        package: The package name.

    Returns:
        True if application needs 32-bit, else 64-bit
    """
    preferredABI = None

    # Match against the primary ABI loaded by the application
    output = device.adb("shell", "pm", "dump", package, "|", "grep", "primaryCpuAbi")
    pattern = re.compile("primaryCpuAbi=(\S+)")
    match = pattern.search(output)

    if match:
        matchABI = match.group(1)
        if matchABI != "null":
            preferredABI = matchABI

    # If that fails match against the default device ABI
    if preferredABI is None:
        preferredABI = device.adb("shell", "getprop", "ro.product.cpu.abi")

    return preferredABI in ["armeabi-v7a", "armeabi"]


def is_gatord_running(device):
    """
    Returns true if an instance of gatord is running on the remote device.

    Args:
        device: The device instance.

    Returns:
        True if running
    """
    # We grep for gatord, but have to filter out the sh and grep commands that
    # also contain the word 'gatord'.
    # In case a capture is running on a renamed gatord, we try to find the
    # external agent forked arg, if the first search fails
    for name in ["gatord", "agent-external"]:
        output = device.adb("shell", "ps", "-ef", "|", "grep", name, "|", "grep", "-v", "grep")
        if len(output.strip()) > 0:
            return True

    return False


def get_connected_devices():
    """
    Get the list of devices that are connected to this host.

    Returns:
        tuple ([(name, model)], [name])
        First tuple element is a list of available device, the second is a list
        of devices that are seen but not accessible (typically because adb has
        not been authorized on the device).
    """
    devices = []

    try:
        adb = Device()
        logFile = adb.adb("devices")

        for line in logFile.splitlines():
            line = line.rstrip()

            # Match devices that are available for adb. Note devices may be
            # flagged as not available if get_device_model() fails, which can
            # happen with dev boards accessed over wired Ethernet.
            if line.endswith("device"):
                deviceName = line.split()[0]
                model = get_device_model(Device(deviceName))
                devices.append((deviceName, model))
            # Match devices that are detectable, but not usable
            elif line.endswith(('offline', 'unauthorized')):
                deviceName = line.split()[0]
                devices.append((deviceName, None))

    except sp.CalledProcessError:
        return (None, None)

    badDevices = sorted([x[0] for x in devices if x[1] is None])
    goodDevices = sorted([x for x in devices if x[1] is not None])
    return (goodDevices, badDevices)


def get_device_name(devName, interactive):
    """
    Helper function to determine which device name to use.

    Args:
        devName: The user-specified device name on the command line. This may
            be a prefix of the full name (not case sensitive), or None
            (auto-select).
        interactive: Is this an interactive session which can use menu prompts?
    """
    goodDvs, badDvs = get_connected_devices()

    # Always log devices that are available
    if badDvs:
        print("\nSearching for devices:")
        for device in badDvs:
            print("    %s found, but not debuggable" % device)

    # No devices found
    if not goodDvs:
        print("ERROR: Device must be connected; none available")
        return None

    # In non-interactive mode or user device check we have unambiguous device
    if not interactive or devName:
        if devName:
            search = devName.lower()
            userDvs = [x for x in goodDvs if x[0].lower().startswith(search)]

            # User device not found ...
            if not userDvs:
                print("ERROR: Device '%s' not found or not usable" % devName)
                return None

            # User device found too many times ...
            if len(userDvs) > 1:
                print("ERROR: Device '%s' is ambiguous" % devName)
                return None

            return userDvs[0][0]

        else:
            # Non-specific devices found too many times
            if len(goodDvs) > 1:
                print("ERROR: Device must be specified; multiple available")
                return None

            return goodDvs[0][0]

    # In interactive mode use the menu selector; print header if not already
    if not badDvs:
        print("\nSearching for devices:")

    for device, model in goodDvs:
        print("    %s / %s found" % (device, model))

    menuEntries = ["%s / %s" % x for x in goodDvs]
    if len(menuEntries) < 1:
        print("\nERROR: Device not selected; none available")
        return None

    deviceIndex = select_from_menu("device", menuEntries)
    if deviceIndex is None:
        print("\nERROR: Device not selected; multiple available")
        return None

    return goodDvs[deviceIndex][0]


def get_gpu_name(device):
    """
    Determine the GPU name from dumpsys queries.

    Immortalis GPUs report in dumpsys as e.g., Mali-G715-Immortalis.

    Args:
        device: The device instance.
    """
    print("\nSearching for an Arm GPU:")
    try:
        logFile = device.adb("shell", "dumpsys", "SurfaceFlinger")
        pattern = re.compile("Mali-([TG][0-9]+)(-Immortalis)?")
        match = pattern.search(logFile)
        if match:
            gpu = match.group(1)
            brand = "Mali" if not match.group(2) else "Immortalis"
            print("    %s-%s GPU found" % (brand, gpu))
        else:
            print("    No Arm GPU found")

    except sp.CalledProcessError:
        print("    Failed to query device")


def get_package_name(device, pkgName, interactive):
    """
    Helper function to determine which package to use.

    Args:
        device: The device instance.
        pkgName: The user-specified package name on the command line. This may
            be the full name (case-sensitive), or None (auto-select).
        interactive: Is this an interactive session which can use menu prompts?
    """
    allPkg = get_package_list(device, showDebuggableOnly=False, showMainIntentOnly=False)

    # In non-interactive mode or with a user-specified package, then check it
    if not interactive or pkgName:
        if not pkgName:
            print("ERROR: Package must be specified")
            return None

        if pkgName not in allPkg:
            print("ERROR: Package '%s' not found" % pkgName)
            return None

        if not is_package_debuggable(device, pkgName):
            print("ERROR: Package '%s' not debuggable" % pkgName)
            return None

        return pkgName

    # In interactive mode without named package then find one, with prompt ...
    print("\nSearching for debuggable packages:")
    pleaseWait = "    Please wait for search to complete..."
    print(pleaseWait, end="\r")
    goodPkg = get_package_list(device, showDebuggableOnly=True, showMainIntentOnly=True)
    plural = "s" if len(goodPkg) != 1 else ""
    message = "    %u debuggable package%s found" % (len(goodPkg), plural)
    template = "\r%%-%us" % len(pleaseWait)
    print(template % message)

    if len(goodPkg) < 1:
        print("\nERROR: No debuggable packages with MAIN activities found")
        return None

    pkgIndex = select_from_menu("debuggable packages", goodPkg)
    if pkgIndex is None:
        print("\nNo package selected, exiting ...")
        return None

    return goodPkg[pkgIndex]


def is_package_debuggable(device, package):
    """
    Test if a package is debuggable.

    Args:
        device: The device instance.
        package: The package name.

    Returns:
        `True` if the package is debuggable, else `False`.
    """
    try:
        subCmd = "if run-as %s true ; then echo %s ; fi" % (package, package)
        logFile = device.adb("shell", subCmd)
        return logFile.strip() == package
    except sp.CalledProcessError:
        return False


def get_package_list(device, showDebuggableOnly, showMainIntentOnly=True):
    """
    Fetch the list of packages on the target device.

    Args:
        device: The device instance.
        showDebuggableOnly: whether the list should show only
            debuggable packages.
        showMainIntentOnly: whether the list should show only
            packages with a MAIN activity.

    Returns:
        The list of packages, or an empty list on error.
    """
    opt = "-3" if showDebuggableOnly else ""
    command = "pm list packages -e %s | sed 's/^package://'" % opt

    if showDebuggableOnly:
        # Test if the package is debuggable on the device
        subCmd0 = "if run-as $0 true ; then echo $0 ; fi"
        command += " | xargs -n1 sh -c '%s' 2> /dev/null" % subCmd0

    if showMainIntentOnly:
        # Test if the package has a MAIN activity
        subCmd1 = "dumpsys package $0 | if grep -q \"android.intent.action.MAIN\" ; then echo $0 ; fi"
        command += " | xargs -n1 sh -c '%s' 2> /dev/null" % subCmd1

    try:
        package_list = device.adb("shell", command).splitlines()

        # some shells (seen on android 10 and 9) report "sh" as a valid package
        if "sh" in package_list:
            package_list.remove("sh")

        return package_list
    except sp.CalledProcessError:
        return []


def push_lwi_config(device, args, package):
    """
    Create a configuration file for LWI and push it onto the device.

    Args:
        args: struct of parameters
    """
    # Semantics are different in the LWI
    tempFileDescriptor, tempPath = tempfile.mkstemp()
    try:
        with os.fdopen(tempFileDescriptor, 'w') as paramFile:
            paramFile.write("ANDROID_USER=0\n")
            paramFile.write("MODE=%s\n" % str(args.lwiMode))
            paramFile.write("FPS_WINDOW=%d\n" % int(args.fpsWindow))
            paramFile.write("FPS_THRESHOLD=%d\n" % int(args.fpsThreshold))
            paramFile.write("FRAME_START=%d\n" % int(args.frameStart))
            paramFile.write("FRAME_END=%d\n" % int(args.frameEnd))
            paramFile.write("MIN_FRAME_GAP=%d\n" % int(args.frameGap))
            paramFile.write("COMPRESS_IMG=%s\n" % int(args.compress))
            paramFile.write("CAPTURE_TIMEOUT=%s\n" % int(args.timeout))
    except IOError:
        infoStr = ("\nERROR: an error occurred when creating a local"
                   " temporary config file: {}\n".format(tempPath))
        print(textwrap.fill(infoStr, LINE_LENGTH))
        return False

    # Push the file onto target
    device_config_file = ANDROID_TMP_DIR + CONFIG_FILE
    device.adb("shell", "rm", "-f", device_config_file)
    device.adb("push", tempPath, device_config_file)
    os.remove(tempPath)

    # Check if file's been actually pushed
    try:
        device.adb("shell", "ls", device_config_file)
    except:
        return False

    # Set permission
    device.adb("shell", "chmod", "666", device_config_file)

    return True


# Write the screenshots into output directory
# TODO: Move this into docstring.
def write_capture(device, outDir, package):

    captureName = "pa_lwi"

    with tempfile.NamedTemporaryFile() as fileHandle:
        # Fetch the results by streaming a tar file; we can't "adb pull"
        # directly for new Android applications due to SELinux policy
        tempName = fileHandle.name
        fileHandle.close()

        device.adb("exec-out", "run-as", package, "tar",
                   "-c", captureName, ">", tempName, text=False, shell=True)

        # Repack the tar file into the required output format
        with tempfile.TemporaryDirectory() as tempDir:
            with tarfile.TarFile(tempName) as tarHandle:
                # Extract the tar file
                tarHandle.extractall(tempDir)

                # Rename to the required name
                oldName = os.path.join(tempDir, captureName)
                for file_name in os.listdir(oldName):
                    shutil.move(os.path.join(oldName, file_name), outDir)

    return True


# Download the capture from the target to the host
# TODO: Move this into docstring.
def pull_capture(device, lwiOutDir, package, cleanUp=True, headless=False):

    # Check if capture exists
    adb_stdout = ""
    try:
        adb_stdout = device.adb("shell", "run-as", package, "ls", "pa_lwi")
    except sp.CalledProcessError:
        # Command failed, there is no capture, nothing to do
        pass

    if "No such file" in adb_stdout or adb_stdout == "":
        if headless:
            print("    INFO: No screen captures found")
        return True

    # Also copy config into pa_lwi dir
    try:
        device_config_file = ANDROID_TMP_DIR + CONFIG_FILE
        device.adb("shell", "run-as", package, "ls", device_config_file)
        device.adb("shell", "run-as", package, "cp", device_config_file, "pa_lwi")
    except:
        print("    WARNING: No configuration file found")

    # Write in host
    if not write_capture(device, lwiOutDir, package):
        return False

    # Clean up
    if cleanUp:
        device.adb("shell", "run-as", package, "rm", "-rf", "pa_lwi")
        device.adb("shell", "run-as", package, "rm", "-rf", "pa_lwi.tar")

    return True


def enable_vulkan_debug_layer(device, args):
    """
    How to load/enable vulkan here will be determined by two things:

       1) What API version the target is running:
           Devices running Android 9 (sdk 28) or above will use sandboxed library within app local storage.
           Devices running lower version of Android will use global layer activation

       2) What version of ndk we built with:
           With NDK r21+ per app layer activation will be possible without access to app's sources.

    Args:
        device: The device instance.
        args: The command arguments.
    """

    print("\nInstalling Vulkan debug layer")

    vkLayerBaseName = os.path.basename(os.path.normpath(args.vkLayerLibPath))

    if vkLayerBaseName != EXPECTED_VULKAN_LAYER_FILE:
        print("\nWARNING: The provided Vulkan layer does not match the expected Vulkan layer name (given: %s, expected: %s); is this correct?\n" % (vkLayerBaseName, EXPECTED_VULKAN_LAYER_FILE,))

    if args.androidVersion < ANDROID_MIN_VULKAN_SDK:
        device.adb("shell", "setprop", "debug.vulkan.layers", EXPECTED_VULKAN_LAYER_NAME)
    else:
        device.adb("push", args.vkLayerLibPath, ANDROID_TMP_DIR)
        device.adb("shell", "run-as", args.package, "cp", ANDROID_TMP_DIR + vkLayerBaseName, ".")
        device.adb("shell", "settings", "put", "global", "enable_gpu_debug_layers", "1")
        device.adb("shell", "settings", "put", "global", "gpu_debug_app", args.package)

        if args.lwiVal:
            device.adb("shell", "settings", "put", "global", "gpu_debug_layers", EXPECTED_VULKAN_LAYER_NAME + ":" + EXPECTED_VALIDATION_LAYER_NAME)
        else:
            device.adb("shell", "settings", "put", "global", "gpu_debug_layers", EXPECTED_VULKAN_LAYER_NAME)


def enable_gles_debug_layer(device, args):
    """
    Args:
        device: The device instance.
        args: The command arguments.
    """
    print("\nInstalling OpenGL ES debug layer")

    glesLayerBaseName = os.path.basename(os.path.normpath(args.glesLayerLibPath))

    if glesLayerBaseName != EXPECTED_GLES_LAYER_FILE_NAME:
        print("\nWARNING: The provided GLES layer does not match the expected GLES layer name (given: %s, expected: %s); is this correct?\n" % (glesLayerBaseName, EXPECTED_GLES_LAYER_FILE_NAME,))

    device.adb("push", args.glesLayerLibPath, ANDROID_TMP_DIR)
    device.adb("shell", "run-as", args.package, "cp", ANDROID_TMP_DIR + glesLayerBaseName, ".")
    device.adb("shell", "settings", "put", "global", "enable_gpu_debug_layers", "1")
    device.adb("shell", "settings", "put", "global", "gpu_debug_app", args.package)
    device.adb("shell", "settings", "put", "global", "gpu_debug_layers_gles", glesLayerBaseName)


def disable_vulkan_debug_layer(device, args):
    """
    Clean up the Vulkan layer installation.

    Args:
        device: The device instance.
        args: The command arguments.
    """
    print("\nDisabling Vulkan debug layer")

    layerBaseName = os.path.basename(os.path.normpath(args.vkLayerLibPath))

    if args.androidVersion < ANDROID_MIN_VULKAN_SDK:
        device.adb("shell", "setprop", "debug.vulkan.layers", "''")
    else:
        device.adb("shell", "settings", "delete", "global", "enable_gpu_debug_layers")
        device.adb("shell", "settings", "delete", "global gpu_debug_app")
        device.adb("shell", "settings", "delete", "global gpu_debug_layers")

    device.adb_quiet("shell", "run-as", args.package, "rm", layerBaseName)


def disable_gles_debug_layer(device, args):
    """
    Clean up the OpenGL ES layer installation.

    Args:
        device: The device instance.
        args: The command arguments.
    """

    print("\nDisabling OpenGL ES debug layer")

    layerBaseName = os.path.basename(os.path.normpath(args.glesLayerLibPath))

    device.adb("shell", "settings", "delete", "global", "enable_gpu_debug_layers")
    device.adb("shell", "settings", "delete", "global gpu_debug_app")
    device.adb("shell", "settings", "delete", "global gpu_debug_layers_gles")

    device.adb_quiet("shell", "run-as", args.package, "rm", layerBaseName)


def clean_gatord(device, package):
    """
    Cleanup gatord and test process on the device.

    Args:
        device: The device instance.
        package: The package name.
    """
    # Kill any prior instances of gatord
    device.adb_quiet("shell", "pkill", "gatord")
    device.adb_quiet("shell", "run-as", package, "pkill", "gatord")

    # Kill any prior instances of the test application
    device.adb_quiet("shell", "am", "force-stop", package)

    # Remove any data files in both temp directory and app directory
    device.adb_quiet("shell", "rm", "-f", "%sgatord" % ANDROID_TMP_DIR)
    device.adb_quiet("shell", "rm", "-f", "%sconfiguration.xml" % ANDROID_TMP_DIR)
    device.adb_quiet("shell", "rm", "-rf", "%s%s.apc" % (ANDROID_TMP_DIR, package))

    # Disable perf counters
    device.adb_quiet("shell", "setprop", "security.perf_harden", "1")


def install_gatord(device, package, gatord, configuration):
    """
    Install the gatord binary and configuration files.

    Args:
        device: The device instance.
        package: The package name.
        gatord: Path to the gatord binary file on the host.
        configuration: Path to the configuration XML file on the host. This
            may be None for non-headless runs.
    """
    # Install gatord
    device.adb("push", gatord, "%sgatord" % ANDROID_TMP_DIR)
    device.adb("shell", "chmod", "0777", "%sgatord" % ANDROID_TMP_DIR)

    # Install gatord counter configuration
    if configuration:
        device.adb("push", configuration, "%sconfiguration.xml" % ANDROID_TMP_DIR)
        device.adb("shell", "chmod", "0666", "%sconfiguration.xml" % ANDROID_TMP_DIR)

    # Enable perf conters
    device.adb("shell", "setprop", "security.perf_harden", "0")


def run_gatord_interactive(device, package):
    """
    Run gatord for an interactive capture session.

    Args:
        device: The device instance,
        package: The package name.
    """
    # Wait for user to do the manual test
    print("\nManual steps:\n")
    print("    1) Configure and profile using Streamline")
    print("    2) Press <Enter> here after capture has completed to finish.")
    input("\nWaiting for data capture ...")


def run_gatord_headless(device, package, outputName, timeout):
    """
    Run gatord for a headless capture session.

    Results are written to disk.

    Args:
        device: The device instance.
        package: The package name.
        outputName: Name of the output directory (*.apc), or file (*.apc.zip).
        timeout: The test scenario capture timeout in seconds.
    """
    # Wait for user to do the manual test
    print("\nRunning headless test:")
    if not timeout:
        print("    Capture set to wait for process exit")
    else:
        print("    Capture set to stop after %s seconds" % timeout)

    # Run gatord
    apcName = "%s.apc" % package
    remoteApcPath = "%s%s" % (ANDROID_TMP_DIR, apcName,)
    device.adb(
        "shell", "%sgatord" % ANDROID_TMP_DIR,
        "--android-pkg", package, "--stop-on-exit", "yes",
        "--max-duration", "%u" % timeout, "--output", remoteApcPath)

    print("    Capture complete, downloading from target")

    with tempfile.TemporaryDirectory() as tempDir:
        # Fetch the results
        device.adb("pull", remoteApcPath, tempDir)

        # Repack the capture directory into the required output format

        # Rename the APC to the required name
        outApcName = os.path.basename(outputName)
        if outApcName.endswith(".zip"):
            outApcName = outApcName[:-4]

        oldName = os.path.join(tempDir, apcName)
        newName = os.path.join(tempDir, outApcName)

        if (oldName != newName):
            os.rename(oldName, newName)

        # Pack as appropriate
        if outputName.endswith(".apc"):
            shutil.move(newName, outputName)
        else:
            # Remove .zip from the path (the shutil function adds it)
            outZipName = outputName[:-4]
            shutil.make_archive(outZipName, "zip", tempDir)


def exit_handler(device, args):
    """
    Exit handler which will ensure gatord is killed.

    Note that no other cleanup is performed, allowing device failure
    post-mortem analysis to be performed.

    Args:
        device: The device instance.
        package: The package name.
    """
    device.adb_quiet("shell", "pkill", "gatord")
    device.adb_quiet("shell", "run-as", args.package, "pkill", "gatord")

    if args.lwiMode != "off":
        try:
            # Disable vulkan layer debug if necessary
            if args.lwiApi == "vulkan":
                disable_vulkan_debug_layer(device, args)

            # Disable gles layer if necessary
            elif args.lwiApi == "gles" and args.androidVersion >= ANDROID_MIN_OPENGLES_SDK:
                disable_gles_debug_layer(device, args)
        except sp.CalledProcessError as e:
            handle_disconnect_error(e)

        device.adb_quiet("shell", "rm", ANDROID_TMP_DIR + CONFIG_FILE)
        device.adb_quiet("shell", "run-as", args.package, "rm", "-rf", "pa_lwi")
        device.adb_quiet("shell", "run-as", args.package, "rm", "-rf", "pa_lwi.tar")


def raise_path_error(message, location, option):
    """
    Format a path-based error message and raise ValueError.

    Args:
        message: The error string.
        location: The file path causing the error.
        option: Which command line option does this apply to?
    """
    # Print absolute paths to ensure useful logs
    location = os.path.abspath(location)
    label = "ERROR: %s.\n       %s (%s)" % (message, location, option)
    raise ValueError(label)


def parse_cli(parser):
    """
    Parse the command line.

    Args:
        parser: The argument parser to populate.

    Returns:
        Return an argparse results object.
    """
    parser.add_argument(
        "--device", "-E", default=None,
        help="the target device name (default=auto-detected)")

    parser.add_argument(
        "--package", "-P", default=None,
        help="the application package name (default=auto-detected)")

    parser.add_argument(
        "--headless", "-H", default=None, metavar="CAPTURE_PATH",
        help="perform a headless capture, writing the result to the path "
             "CAPTURE_PATH (default=perform interactive capture)")

    parser.add_argument(
        "--headless-timeout", "-T", dest="timeout", type=int, default=0,
        help="exit the headless timeout after this many seconds "
             "(default=wait for process exit)")

    parser.add_argument(
        "--config", "-C",  dest="config", default=None, type=ap.FileType('r'),
        help="the capture counter config XML file to use (default=None for "
             "interactive, configuration.xml for headless)")

    parser.add_argument(
        "--daemon", "-D", default=None,
        help="the path to the gatord binary to use (default=gatord)")

    parser.add_argument(
        "--overwrite", action="store_true", default=False,
        help="overwrite an earlier headless output (default=disabled)")

    parser.add_argument(
        "--verbose", "-v", action="store_true", default=False,
        help="enable verbose logging (default=disabled)")

    parser.add_argument(
        "--lwi-mode", "-M", dest="lwiMode", default="off",
        choices=["off", "counters", "screenshots"],
        help="AF@Select layer mode. Possible values are 'off', 'counters', or 'screenshots' (default=off).\n"
             "  - off: Do not use an interceptor. The application must provide frame boundary annotations\n"
             "         if they want to generate Performance Advisor reports. \n"
             "  - counters: Use an interceptor to provide frame boundaries and counters.\n"
             "  - screenshots: Use an interceptor to provide frame boundaries, counters, and screenshots.\n")

    parser.add_argument(
        "--lwi-api", dest="lwiApi", default="gles", choices=["gles", "vulkan"],
        help="The API to listen to. Possible values are 'gles' or 'vulkan' (default=gles).")

    parser.add_argument(
        "--lwi-gles-layer-lib-path", dest="glesLayerLibPath", default="",
        help="The OpenGL ES layer library path.")

    parser.add_argument(
        "--lwi-vk-layer-lib-path", dest="vkLayerLibPath", default="",
        help="The Vulkan layer library path.")

    parser.add_argument(
        "--lwi-fps-window", "-W", dest="fpsWindow", type=int, default=6,
        help="Size (in frames) of the sliding window used for FPS calculation")

    parser.add_argument(
        "--lwi-fps-threshold", "-Th", dest="fpsThreshold", type=int, default=30,
        help="Perform capture if FPS goes under this threshold. Default is 30")

    parser.add_argument(
        "--lwi-frame-start", "-S", dest="frameStart", type=int, default=1,
        help="Start tracking from frame number.")

    parser.add_argument(
        "--lwi-frame-end", "-N", dest="frameEnd", type=int, default=-1,
        help="End tracking at frame number.")

    parser.add_argument(
        "--lwi-frame-gap", "-G", dest="frameGap", type=int, default=200,
        help="Minimum number of frames between two captures.")

    parser.add_argument(
        "--lwi-out-dir", "-o", dest="outDir", default=None,
        help="Directory where the LWI capture is output.")

    parser.add_argument(
        "--lwi-compress-img", "-X", dest="compress", action="store_true", default=False,
        help="If set the layer will store compressed frame captures. Default is unset.")

    # Internal development option that allows Khronos validation to be enabled underneath LWI
    parser.add_argument(
        "--lwi-val", dest="lwiVal", default=False, action="store_true",
        help=ap.SUPPRESS)

    args = parser.parse_args()

    # Validate the numeric args
    if args.fpsWindow < 1:
        raise ValueError("ERROR: --lwi-fps-window must greater than zero")
    if args.frameGap < 1:
        raise ValueError("ERROR: --lwi-frame-gap must be greater than zero")
    if args.fpsThreshold < 1:
        raise ValueError("ERROR: --lwi-fps-threshold must be greater than zero")
    if args.frameStart < 1:
        raise ValueError("ERROR: --lwi-frame-start must be greater than zero")
    if args.frameEnd != -1 and args.frameEnd < 1:
        raise ValueError("ERROR: --lwi-frame-end must be greater than zero, or -1 (no end)")
    if args.frameEnd != -1 and args.frameEnd <= args.frameStart:
        raise ValueError("ERROR: --lwi-frame-end must be greater than --lwi-frame-start, or -1 (no end)")
    if args.headless and args.timeout < 0:
        raise ValueError("ERROR: --headless-timeout must be greater than or equal to zero")

    if args.config:
        args.config = args.config.name

    if args.headless and not args.config:
        args.config = "configuration.xml"

    if not args.headless:
        args.timeout = 0

    # Check if the config file exists.
    if args.config and not os.path.exists(args.config):
        raise ValueError("ERROR: Could not find config file '%s'" % args.config)

    # Check that the headless path has a valid extension
    if args.headless:
        isAPC = args.headless.endswith(".apc")
        isZIP = args.headless.endswith(".apc.zip")
        if (not isAPC) and (not isZIP):
            raise_path_error("Headless output must be a *.apc dir or a *.apc.zip file",
                             args.headless, "--headless")

        dirname = os.path.dirname(args.headless)
        if dirname and not os.path.exists(dirname):
            raise_path_error("Headless output parent directory does not exist",
                             dirname, "--headless")

        if dirname and not os.access(dirname, os.W_OK):
            raise_path_error("Headless output parent directory must be writable",
                             dirname, "--headless")

        if os.path.exists(args.headless):
            if not args.overwrite:
                raise_path_error("Headless output already exists and --overwrite not set",
                                 args.headless, "--headless")

            # If overwrite enabled then remove the old files
            if os.path.isfile(args.headless):
                os.remove(args.headless)
            else:
                shutil.rmtree(args.headless)

    # Ensure the lwi output path is absolute
    # Use a default path if the user hasn't specified it
    if args.lwiMode != "off":
        if args.outDir is None:
            args.outDir = default_lwi_dir()
        else:
            args.outDir = os.path.abspath(args.outDir)

    return args


def is_a_directory(device, path_to_test):
    is_directory = device.adb("shell", "if [ -d %s ] ; then echo d ; fi" % path_to_test)
    return len(is_directory) > 0


def has_adb():
    """
    Check that the user has adb on PATH
    """
    return shutil.which("adb") is not None


def print_lwi_args_info(args):
    if args.lwiMode == "off":
        infoStr = ("--lwi-mode=off, frame marker annotations will not be added to your"
                   " application and screenshots will not be taken")
        print(textwrap.fill(infoStr, LINE_LENGTH))

    if args.lwiMode == "screenshots":
        print("--lwi-fps-threshold={}".format(args.fpsThreshold))

        infoStr = ("--lwi-mode=screenshots, screenshots will be captured when fps drops "
                   "below --lwi-fps-threshold")
        print(textwrap.fill(infoStr, LINE_LENGTH))

    if args.lwiMode == "counters":
        infoStr = ("--lwi-mode=counters, screenshots will not be captured.")
        print(textwrap.fill(infoStr, LINE_LENGTH))

    if args.lwiMode != "off":
        infoStr = ("--lwi-api={}".format(args.lwiApi))
        print(textwrap.fill(infoStr, LINE_LENGTH))

    print("--lwi-out-dir={}".format(args.outDir))


def default_lwi_dir():
    lwiOutDir = os.path.abspath('lwi-out-{:%d%m%y-%H%M%S}'.format(datetime.datetime.now()))
    print("--lwi-out-dir not specified, will use default directory {}".format(lwiOutDir))
    return lwiOutDir


def get_script_dir():
    return os.path.dirname(os.path.realpath(__file__))


def get_daemon(isArm32):
    scriptDir = get_script_dir()
    daemonName = "gatord"
    sameDirPath = os.path.join(scriptDir, daemonName)
    if os.path.exists(sameDirPath):
        return sameDirPath

    dirName = "arm" if isArm32 else "arm64"
    return os.path.join(
        scriptDir, os.pardir, os.pardir, os.pardir, "streamline", "bin", "android", dirName, daemonName)


def get_default_lib_layer_path(isArm32, layerName):
    scriptDir = get_script_dir()

    sameDirPath = os.path.join(scriptDir, layerName)
    if os.path.exists(sameDirPath):
        return sameDirPath

    dirName = "arm" if isArm32 else "arm64"
    return os.path.join(scriptDir, dirName, layerName)

def ensure_lwi_output_path_usable(abs_lwi_out_dir, overwrite):
    # Check that the LWI capture output directory is empty and writable.

    if os.path.exists(abs_lwi_out_dir):
        if not overwrite:
            raise_path_error("Report output already exists and --overwrite not set",
                             abs_lwi_out_dir, "--lwi-out-dir")

        # If overwrite enabled then remove the old files
        if os.path.isfile(abs_lwi_out_dir):
            os.remove(abs_lwi_out_dir)
        else:
            shutil.rmtree(abs_lwi_out_dir)

    if not os.path.exists(abs_lwi_out_dir):
        try:
            os.makedirs(abs_lwi_out_dir)
        except PermissionError:
            raise_path_error("Report output could not be created, is parent directory writable?",
                             abs_lwi_out_dir, "--lwi-out-dir")

    if not os.path.isdir(abs_lwi_out_dir):
        raise_path_error("Report output directory already exists and --overwrite not set",
                         abs_lwi_out_dir, "--lwi-out-dir")

    if len(os.listdir(abs_lwi_out_dir)) > 0:
        raise_path_error("Report output directory already exists and --overwrite not set",
                         abs_lwi_out_dir, "--lwi-out-dir")

    if not os.access(abs_lwi_out_dir, os.W_OK):
        raise_path_error("Report output directory must be writable",
                         abs_lwi_out_dir, "--lwi-out-dir")


def handle_disconnect_error(e):
    print("ERROR: Unexpected error while running gatord on device, unable to run command:", file=sys.stderr, end=" ")
    print("\"", end=" ", file=sys.stderr)
    print(*e.cmd, file=sys.stderr, end=" ")
    print("\"", file=sys.stderr)
    print("Please ensure device remains on while gatord is running.", file=sys.stderr)
    sys.exit(1)


def main():
    """
    Script main function.

    Returns:
        Process return code.
    """
    parser = ap.ArgumentParser(formatter_class=ArgFormatter)
    try:
        args = parse_cli(parser)
    except ValueError as err:
        parser.print_usage()
        print("{0}".format(err))
        return 4

    if args.verbose:
        global DEBUG_GATORD
        DEBUG_GATORD = True
        print_lwi_args_info(args)

    # Validate basic command line option sanity
    if args.headless:
        isAPC = args.headless.endswith(".apc")
        isZIP = args.headless.endswith(".apc.zip")
        if (not isAPC) and (not isZIP):
            print("ERROR: Outputs must be a *.apc dir or a *.apc.zip file")
            return 1

        if not os.path.isfile(args.config):
            print("ERROR: Headless requires explicit --config or a "
                  "configuration.xml in the current working directory")
            return 1

        dirname = os.path.dirname(args.headless)
        if dirname and not os.path.exists(dirname):
            print("ERROR: Headless output directory '%s' does not exist"
                  % dirname)
            return 1

        if os.path.exists(args.headless):
            if not args.overwrite:
                print("ERROR: Headless output location already exists and "
                      " --overwrite not specified")
                return 1

            # If overwrite enabled then remove the old files
            if os.path.isfile(args.headless):
                os.remove(args.headless)
            else:
                shutil.rmtree(args.headless)

    # Now check that adb is present
    if not has_adb():
        print("ERROR: adb not found. Make sure adb is installed and on your PATH")
        return 1

    # Select a specific target device, or fail if we cannot
    deviceName = get_device_name(args.device, not args.headless)
    if not deviceName:
        return 2

    device = Device(deviceName)

    # Store the device android version
    args.androidVersion = get_android_version(device)
    if args.androidVersion < ANDROID_MIN_SUPPORTED_VERSION:
        print("\nWARNING: Android version on device is < Android 10. Arm only supports Android versions 10 or higher.")

    # Test if this is a supported device and fail early if it's not
    if args.lwiMode != "off":
        if args.lwiApi == "vulkan" and args.androidVersion < ANDROID_MIN_VULKAN_SDK:
            print("\nERROR: Using the Vulkan layer requires a device with Android 9 or higher.")
            print("       For older devices, custom annotations in the application can be used")
            print("       with --lwi=off to generate the frame boundaries needed.")
            return 4
        elif args.lwiApi == "gles" and args.androidVersion < ANDROID_MIN_OPENGLES_SDK:
            print("\nERROR: Using the OpenGL ES layer requires a device with Android 10 or higher.")
            print("       For older devices, custom annotations in the application can be used")
            print("       with --lwi=off to generate the frame boundaries needed.")
            return 4

    gatord_device_path = "%sgatord" % ANDROID_TMP_DIR
    if is_a_directory(device, gatord_device_path):
        print("\nERROR: Unexpected directory on the device, at '%s'.  Ensure nothing resides at the location then retry." % gatord_device_path)
        return 1

    # Select a specific package, or fail if we cannot
    package = get_package_name(device, args.package, not args.headless)
    if not package:
        return 3
    else:
        args.package = package

    isArm32 = is_package_32bit_abi(device, package)

    # Note: this is no longer technical required; gator now reliably
    # auto-detects the Mali GPU, but the information is useful for the user
    # to know in terms of selecting the correct template
    get_gpu_name(device)

    # Remove any old files
    clean_gatord(device, package)

    # If gatord is needed, install it
    if args.headless:
        # Check if daemon exists in host
        gatordLocation = args.daemon
        if gatordLocation is None:
            gatordLocation = get_daemon(isArm32)

        if not os.path.exists(gatordLocation):
            print("\nERROR: Could not find daemon file '%s'" % gatordLocation)
            return 4

        # Install new gatord files
        print("\nInstalling new gatord files")
        install_gatord(device, package, gatordLocation, args.config)

    # If LWI activated, make and upload config file
    if args.lwiMode != "off":
        try:
            ensure_lwi_output_path_usable(args.outDir, args.overwrite)
        except ValueError as err:
            print("{0}".format(err))
            return 4

        if not push_lwi_config(device, args, package):
            print("ERROR: Failed to upload the LWI configuration onto the device")
            return 4

        # If we are tracing vulkan
        if args.lwiApi == "vulkan":
            # We need vulkan layer when device running Android >= 9
            if args.androidVersion >= ANDROID_MIN_VULKAN_SDK:
                vkLayerLibPath = args.vkLayerLibPath
                if vkLayerLibPath == "":
                    vkLayerLibPath = get_default_lib_layer_path(isArm32, EXPECTED_VULKAN_LAYER_FILE)

                if not os.path.isfile(vkLayerLibPath):
                    print("\nERROR: Couldn't find the vulkan layer, specify with --lwi-vk-layer-lib-path.")
                    return 4

                args.vkLayerLibPath = vkLayerLibPath
                enable_vulkan_debug_layer(device, args)
        else:
            # We need gles layer when device running Android >= 10
            if args.androidVersion >= ANDROID_MIN_OPENGLES_SDK:
                glesLayerLibPath = args.glesLayerLibPath
                if glesLayerLibPath == "":
                    glesLayerLibPath = get_default_lib_layer_path(isArm32, EXPECTED_GLES_LAYER_FILE_NAME)

                if not os.path.isfile(glesLayerLibPath):
                    print("\nERROR: Couldn't find the gles layer, specify with --lwi-gles-layer-lib-path.")
                    return 4

                args.glesLayerLibPath = glesLayerLibPath
                enable_gles_debug_layer(device, args)

    atexit.register(exit_handler, device, args)

    # Run the test scenario
    try:
        if args.headless is None:
            run_gatord_interactive(device, package)
        else:
            run_gatord_headless(device, package, args.headless, args.timeout)

        # Clean and download LWI capture
        if args.lwiMode != "off":
            # Pulling the capture from device
            print("\nPulling capture from device")
            if not pull_capture(device, args.outDir, package, headless=args.headless):
                print("\nERROR: Failed to pull capture from the device.")
            device.adb_quiet("shell", "rm", ANDROID_TMP_DIR + CONFIG_FILE)

            # Disable vulkan layer debug if necessary
            if args.lwiApi == "vulkan":
                disable_vulkan_debug_layer(device, args)

            # Disable gles layer if necessary
            if args.lwiApi == "gles":
                disable_gles_debug_layer(device, args)

        atexit.unregister(exit_handler)

        # Remove any new files if requested to do so
        clean_gatord(device, package)

    except sp.CalledProcessError as e:
        atexit.unregister(exit_handler)
        handle_disconnect_error(e)
        return 1

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n\nERROR: User interrupted execution")
