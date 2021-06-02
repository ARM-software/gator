#!/usr/bin/env python3
# =============================================================================
#  This confidential and proprietary software may be used only as authorized by
#  a licensing agreement from Arm Limited.
#       (C) COPYRIGHT 2019 Arm Limited, ALL RIGHTS RESERVED
#  The entire notice above must be reproduced on all authorized copies and
# copies may only be made to the extent permitted by a licensing agreement from
# Arm Limited.
# =============================================================================
"""
The `gator_me.py` script helps set up either an interactive Streamline capture,
or a headless gatord capture, for a single debuggable package running on a
non-rooted Android device. This script requires Python 3.5 or higher.


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

    python gator_me.py --package <name>

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

    python gator_me.py --package <name> --headless <output>

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


Power-user arguments
====================

By default the script will clean up the device before the test to stop old
files and processes being used by mistake. This can be disabled by setting
`--no-clean-start`, but this is not recommended.

By default the script will clean up the device after the test to leave the
device in a clean state for the next test. This can be disabled by setting
`--no-clean-end`, but this is not recommended. In particular note that
uninstalling the test application before killing the `gatord` process will
render `gatord` unkillable. No further profiling will be possible until the
target device is power cycled.


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

try:
    import argparse as ap
    import atexit
    import math
    import os
    import re
    import shlex
    import shutil
    import subprocess as sp
    import sys
    import tarfile
    import tempfile
    import time

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

bounce_dir = "/data/local/tmp/"

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
            print(message % (0, "Exit gator_me.py"))

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


def get_device_model(device):
    """
    Get the model of a given device

    Args:
        device: The device instance.

    Returns:
        The device model.
    """
    logFile = device.adb("shell", "getprop", "ro.product.model")
    return logFile.strip()


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

            # Match devices that are available for adb
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

    Args:
        device: The device instance.

    Returns:
        The Mali GPU name if found, else None.
    """
    try:
        print("\nSearching for a Mali GPU:")
        logFile = device.adb("shell", "dumpsys", "SurfaceFlinger")
        pattern = re.compile("Mali-([TG][0-9]+)")
        match = pattern.search(logFile)
        if match:
            gpu = match.group(1)
            print("    Mali-%s GPU found" % gpu)
            return gpu
        else:
            print("    No Mali GPU found")
            return None

    except sp.CalledProcessError:
        return None


def get_package_name(device, pkgName, interactive):
    """
    Helper function to determine which package to use.

    Args:
        device: The device instance.
        pkgName: The user-specified package name on the command line. This may
            be the full name (case-sensitive), or None (auto-select).
        interactive: Is this an interactive session which can use menu prompts?
    """
    allPkg = get_package_list(device, showDebuggableOnly=False)

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
    goodPkg = get_package_list(device, showDebuggableOnly=True)
    plural = "s" if len(goodPkg) != 1 else ""
    message = "    %u debuggable package%s found" % (len(goodPkg), plural)
    template = "\r%%-%us" % len(pleaseWait)
    print(template % message)

    if len(goodPkg) < 1:
        print("\nERROR: No debuggable packages found")
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


def get_package_list(device, showDebuggableOnly):
    """
    Fetch the list of packages on the target device.

    Args:
        device: The device instance.
        showDebuggableOnly: whether the list should show only
            debuggable packages.

    Returns:
        The list of packages, or an empty list on error.
    """
    command = "pm list packages | sed 's/^package://'"

    if showDebuggableOnly:
        # Test if the package is debuggable on the device
        subCmd = "if run-as $0 true ; then echo $0 ; fi"
        command += " | xargs -n1 sh -c '%s' 2> /dev/null" % subCmd

    try:
        logFile = device.adb("shell", command)
        return logFile.splitlines()
    except sp.CalledProcessError:
        return []

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

    # Remove any data files in both bounce directory and app directory
    adir = "/data/data/%s/" % package
    device.adb_quiet("shell", "rm", "-f", "%sgatord" % bounce_dir)
    device.adb_quiet("shell", "rm", "-f", "%sconfiguration.xml" % bounce_dir)
    device.adb_quiet("shell", "rm", "-rf", "%s%s.apc" % (bounce_dir, package))
    target = "%sgatord" % adir
    device.adb_quiet("shell", "run-as", package, "rm", "-f", target)
    target = "%sconfiguration.xml" % adir
    device.adb_quiet("shell", "run-as", package, "rm", "-f", target)
    target = "%s%s.apc" % (adir, package)
    device.adb_quiet("shell", "run-as", package, "rm", "-rf", target)

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
    adir = "/data/data/%s/" % package
    device.adb("push", gatord, "%sgatord" % bounce_dir)
    device.adb("shell", "chmod", "0777", "%sgatord" % bounce_dir)
    device.adb("shell", "run-as", package, "cp", "%sgatord" % bounce_dir, adir)

    # Install gatord counter configuration
    if configuration:
        device.adb("push", configuration, "%sconfiguration.xml" % bounce_dir)
        device.adb("shell", "chmod", "0666", "%sconfiguration.xml" % bounce_dir)
        device.adb("shell", "run-as", package, "cp",
                   "%sconfiguration.xml" % bounce_dir, adir)

    # Enable perf conters
    device.adb("shell", "setprop", "security.perf_harden", "0")


def run_gatord_interactive(device, package):
    """
    Run gatord for an interactive capture session.

    Args:
        device: The device instance,
        package: The package name.
    """

    # Run gatord
    commands = [
        "shell", "run-as", package, "/data/data/%s/gatord" % package,
        "--wait-process", package, "-p", "uds"]

    if DEBUG_GATORD:
        commands.append("-d")

    process = device.adb_async(*commands)

    # Sleep one second to let device software boot up
    time.sleep(1)

    if process.poll():
        print("    ERROR: Unexpected exit of gatord, run with -v for details")
        return

    # Wait for user to do the manual test
    print("\nManual steps:")
    print("    1) Make sure that you have the path to the 'adb' ")
    print("       executable configured in Streamline under ")
    print("       Preferences -> External Locations or Window -> ")
    print("       Preferences -> External Tools. The path must be ")
    print("       the full path to 'adb' / 'adb.exe' command.")
    print("    2) Using the 'Start' view, TCP (Advanced) option ")
    print("       in Streamline, enter adb:%s as" % device.device)
    print("       the target address, or select the device under ")
    print("       \"Choose an existing target\"")
    print("    3) Start data capture from Streamline")
    print("    4) Run test scenario on your device")
    print("    5) Stop data capture from Streamline")
    print("    6) Press <Enter> here after capture has completed")
    input("\nWaiting for data capture ...")
    process.terminate()


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
    device.adb(
        "shell", "run-as", package, "/data/data/%s/gatord" % package,
        "--wait-process", package, "--stop-on-exit", "yes",
        "--max-duration", "%u" % timeout, "--output", apcName)

    print("    Capture complete, downloading from target")

    with tempfile.NamedTemporaryFile() as fileHandle:
        # Fetch the results by streaming a tar file; we can't "adb pull"
        # directly for new Android applications due to SELinux policy
        tempName = fileHandle.name
        fileHandle.close()
        device.adb(
            "exec-out", "run-as", package, "tar", "-c", apcName, ">", tempName,
            text=False, shell=True)

        # Repack the tar file into the required output format
        with tempfile.TemporaryDirectory() as tempDir:
            with tarfile.TarFile(tempName) as tarHandle:
                # Extract the tar file
                tarHandle.extractall(tempDir)

                # Rename the APC to the required name
                outApcName = os.path.basename(outputName)
                if outApcName.endswith(".zip"):
                    outApcName = outApcName[:-4]

                oldName = os.path.join(tempDir, apcName)
                newName = os.path.join(tempDir, outApcName)
                os.rename(oldName, newName)

                # Pack as appropriate
                if outputName.endswith(".apc"):
                    shutil.move(newName, outputName)
                else:
                    # Remove .zip from the path (the shutil function adds it)
                    outZipName = outputName[:-4]
                    shutil.make_archive(outZipName, "zip", tempDir)


def exit_handler(device, package):
    """
    Exit handler which will ensure gatord is killed.

    Note that no other cleanup is performed, allowing device failure
    post-mortem analysis to be performed.

    Args:
        device: The device instance.
        package: The package name.
    """
    print("\nCleaning up device ...")
    device.adb_quiet("shell", "pkill", "gatord")
    device.adb_quiet("shell", "run-as", package, "pkill", "gatord")


def parse_cli():
    """
    Parse the command line.

    Returns:
        Return an argparse results object.
    """
    parser = ap.ArgumentParser()

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
        "--daemon", "-D", default="gatord", type=ap.FileType('r'),
        help="the path to the gatord binary to use (default=gatord)")

    parser.add_argument(
        "--no-clean-start", dest="cleanS", action="store_false", default=True,
        help="disable pre-run device cleanup (default=enabled)")

    parser.add_argument(
        "--no-clean-end", dest="cleanE", action="store_false", default=True,
        help="disable post-run device cleanup (default=enabled)")

    parser.add_argument(
        "--overwrite", action="store_true", default=False,
        help="overwrite an earlier headless output (default=disabled)")

    parser.add_argument(
        "--verbose", "-v", action="store_true", default=False,
        help="enable verbose logging (default=disabled)")

    args = parser.parse_args()

    # Translate files to names (this lets argparse do the error checking)
    args.daemon = args.daemon.name

    if args.config:
        args.config = args.config.name

    if args.headless and not args.config:
        args.config = "configuration.xml"

    return args

def is_a_directory(device, path_to_test):
    is_directory = device.adb("shell", "if [ -d %s ] ; then echo d ; fi" % path_to_test)
    return len(is_directory) > 0

def has_adb():
    """
    Check that the user has adb on PATH
    """
    return shutil.which("adb") is not None

def main():
    """
    Script main function.

    Returns:
        Process return code.
    """
    args = parse_cli()

    if args.verbose:
        global DEBUG_GATORD
        DEBUG_GATORD = True

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

    gatord_device_path = "%sgatord" % bounce_dir
    if is_a_directory(device, gatord_device_path):
        print("\nERROR: Unexpected directory on the device, at '%s'.  Ensure nothing resides at the location then retry." % gatord_device_path)
        return 1

    # Select a specific package, or fail if we cannot
    package = get_package_name(device, args.package, not args.headless)
    if not package:
        return 3

    # Note: this is no longer technical required; gator now reliably
    # auto-detects the Mali GPU, but the information is useful for the user
    # to know in terms of selecting the correct template
    get_gpu_name(device)

    # Remove any old files if requested to do so
    if args.cleanS:
        print("\nCleaning device of old gatord files")
        clean_gatord(device, package)

    # Install new gatord files
    print("\nInstalling new gatord files")
    install_gatord(device, package, args.daemon, args.config)

    atexit.register(exit_handler, device, package)

    # Run the test scenario
    if args.headless is None:
        run_gatord_interactive(device, package)
    else:
        run_gatord_headless(device, package, args.headless, args.timeout)

    atexit.unregister(exit_handler)

    # Remove any new files if requested to do so
    if args.cleanE:
        print("\nCleaning up device ...")
        clean_gatord(device, package)

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n\nERROR: User interrupted execution")
