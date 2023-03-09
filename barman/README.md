# Generate Barman Script
This script takes a user-provided configuration file, which describes the PMU events for sampling, and generates combined barman.c/barman.h files that you can include in a target project. You can generate Barman XML configuration files manually, or you can use the Streamline wizard. See the [Streamline Target Setup Guide for Bare-metal Applications](https://developer.arm.com/documentation/101815/latest/), which describes how to configure Barman and integrate the generated files into your projects.

## Usage
You can use either the Windows Batch file (generate.cmd) or Bash shell script (generate.sh), found in this folder, to run the script. Using either of these commands sets the Barman source directory path automatically. You can override the source directory path by using the `-d` option.
Alternatively, the script is a python module so you can run it directly using the `python -m generate_barman <args>` syntax. In this mode, the Barman source directory path is not set automatically and you must use the `-d` option.

### Windows
```
generate.cmd [-h] [-d BARMAN_DIR] [-o OUTPUT_DIR] [-c BARMAN_CFG]
```

### Linux
```
./generate.sh [-h] [-d BARMAN_DIR] [-o OUTPUT_DIR] [-c BARMAN_CFG]
```

```
options:
  -h, --help            show this help message and exit
  -d BARMAN_DIR, --barman-dir BARMAN_DIR
                        Path to the directory containing the Barman sources.
  -o OUTPUT_DIR, --output-dir OUTPUT_DIR
                        Store the generated files in this directory.
  -c BARMAN_CFG, --barman-cfg BARMAN_CFG
                        Path to the barman.xml configuration file.
```
