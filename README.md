# openpilot-replay

**openpilot-replay** is a standalone C++ implementation of the **[openpilot replay tool](https://github.com/commaai/openpilot/tree/master/tools/replay)**.

It allows you to **replay a recorded driving route** and publish all logged messages as if the system were running live, enabling analysis, visualization, debugging, and tooling integration.

## Purpose

This repository provides a **high-performance, self-contained replay tool** that can be built, run, and extended independently of the openpilot monorepo. It aims to be:

- **Standalone** — works without the full openpilot stack
- **Compatible** — works with existing openpilot data formats and workflows
- **Efficient** — optimized for C++ performance while preserving replay fidelity

## Prerequisites

Before running or compiling **openpilot-replay**, install these dependencies.

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y g++ clang capnproto libcurl4-openssl-dev libzmq3-dev libssl-dev libbz2-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libavdevice-dev libavfilter-dev libffi-dev libgles2-mesa-dev libglfw3-dev libglib2.0-0 libjpeg-dev libncurses5-dev libusb-1.0-0-dev libzstd-dev libcapnp-dev opencl-headers ocl-icd-libopencl1 ocl-icd-opencl-dev

```

## Clone, Compile

### Clone the repository

```bash
git clone https://github.com/deanlee/openpilot-replay.git
cd openpilot-replay
git submodule update --init --recursive
```

### Install Python packages

```bash
python3 -m pip install --upgrade pip
python3 -m pip install scons numpy cython
```

### Build

```bash
scons
```

## Download Precompiled Binary

You can also download a precompiled binary from the [Releases](https://github.com/deanlee/openpilot-replay/releases) page:

```bash
# Make executable
chmod +x replay-linux-x86_64

# Run the demo route
./replay-linux-x86_64 --demo
```

## Setup

Before starting a replay, you need to authenticate with your comma account using `auth.py`. This will allow you to access your routes from the server.

```bash
# Authenticate to access routes from your comma account:
python3 openpilot/tools/lib/auth.py
```

## Replay a Remote Route
You can replay a route from your comma account by specifying the route name.

```bash
# Start a replay with a specific route:
replay <route-name>

# Example:
replay 'a2a0ccea32023010|2023-07-27--13-01-19'

# Replay the default demo route:
replay --demo
```

## Replay a Local Route
To replay a route stored locally on your machine, specify the route name and provide the path to the directory where the route files are stored.

```bash
# Replay a local route
replay <route-name> --data_dir="/path_to/route"

# Example:
# If you have a local route stored at /path_to_routes with segments like:
# a2a0ccea32023010|2023-07-27--13-01-19--0
# a2a0ccea32023010|2023-07-27--13-01-19--1
# You can replay it like this:
replay "a2a0ccea32023010|2023-07-27--13-01-19" --data_dir="/path_to_routes"
```

## Send Messages via ZMQ
By default, replay sends messages via MSGQ. To switch to ZMQ, set the ZMQ environment variable.

```bash
# Start replay and send messages via ZMQ:
ZMQ=1 replay <route-name>
```

## Usage
For more information on available options and arguments, use the help command:

``` bash
$ replay -h
Usage: replay [options] route
Mock openpilot components by publishing logged messages.

Options:
  -h, --help             Displays this help.
  -a, --allow <allow>    whitelist of services to send (comma-separated)
  -b, --block <block>    blacklist of services to send (comma-separated)
  -c, --cache <n>        cache <n> segments in memory. default is 5
  -s, --start <seconds>  start from <seconds>
  -x <speed>             playback <speed>. between 0.2 - 3
  --demo                 use a demo route instead of providing your own
  --auto                 Auto load the route from the best available source (no video):
                         internal, openpilotci, comma_api, car_segments, testing_closet
  --data_dir <data_dir>  local directory with routes
  --prefix <prefix>      set OPENPILOT_PREFIX
  --dcam                 load driver camera
  --ecam                 load wide road camera
  --no-loop              stop at the end of the route
  --no-cache             turn off local cache
  --qcam                 load qcamera
  --no-hw-decoder        disable HW video decoding
  --no-vipc              do not output video
  --all                  do output all messages including uiDebug, userBookmark.
                         this may causes issues when used along with UI

Arguments:
  route                  the drive to replay. find your drives at
                         connect.comma.ai
```

## Visualize the Replay in the openpilot UI
To visualize the replay within the openpilot UI, run the following commands:

```bash
replay <route-name>
cd openpilot/selfdrive/ui && ./ui.py
```

## Work with plotjuggler
If you want to use replay with plotjuggler, you can stream messages by running:

```bash
replay <route-name>
openpilot/tools/plotjuggler/juggle.py --stream
```

## watch3

watch all three cameras simultaneously from your comma three routes with openpilot watch3

simply replay a route using the `--dcam` and `--ecam` flags:

```bash
# start a replay
./replay --demo --dcam --ecam

# then start watch3
cd openpilot/selfdrive/ui && ./watch3.py
```
