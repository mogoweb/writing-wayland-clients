# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This repository contains a Chinese tutorial on writing Wayland client applications, based on "Writing Wayland Clients". The tutorial covers Wayland protocol fundamentals from basic window creation to advanced topics like window decoration, input handling, and system tray integration.

Example code is organized by chapter in the `code/` directory (ch02-ch07).

## Build Requirements

Install development packages:

```bash
sudo apt install build-essential libwayland-dev wayland-protocols libwayland-bin libcairo2-dev libdbus-1-dev
```

## Building Wayland Protocol Files

Wayland protocol XML files must be converted to C code using `wayland-scanner` before compilation:

```bash
# Generate client header
wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h

# Generate implementation code
wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c
```

Note: Some older Makefiles use `code` instead of `private-code`; `private-code` is the preferred modern syntax.

## Common Compilation Pattern

```bash
gcc main.c protocol-code.c -lwayland-client -o runme
```

For drawing with Cairo:
```bash
gcc main.c protocol-code.c -lwayland-client -lcairo -o runme
```

For D-Bus integration:
```bash
gcc main.c ... -o runme $(shell pkg-config --cflags dbus-1) $(shell pkg-config --libs dbus-1)
```

## Protocol Locations

Common protocol XML paths on Debian/Ubuntu systems:
- xdg-shell: `/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml`
- xdg-decoration: `/usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml`
- xdg-foreign: `/usr/share/wayland-protocols/unstable/xdg-foreign/xdg-foreign-unstable-v2.xml`

## Running Samples

Each sample directory contains a Makefile. Build and run:

```bash
cd code/chXX/sampleX-X
make
./runme
```

Use `make clean` to remove built artifacts.
