# VISA Plugin

The VISA Plugin provides support for communicating with instruments using the NI-VISA protocol within the [Instrument Script Server](https://github.com/falcon-autotuning/instrument-script-server) ecosystem.

This plugin enables command execution over a variety of instrument interfaces supported by VISA, including GPIB, USB, Ethernet (TCP/IP), and serial connections.

---

## Overview

The VISA Plugin implements the [Instrument Plugin API](https://github.com/falcon-autotuning/instrument-plugin-api) and translates high-level commands into VISA operations. It supports:

- Command execution using VISA sessions
- Automatic parsing of scalar and array responses
- Shared-memory buffer integration for transferring large datasets
- Cross-platform operation with:
  - [NI-VISA](https://www.ni.com/en/support/downloads/drivers/download.ni-visa.html?srsltid=AfmBOoo-MCRpCpznNX_cXmaxOHTNgtJ9dBDsDFPycvVyq-qLI6DWK00B) on Windows
  - Stub implementation for development and testing on Linux (optionally NI-VISA if your OS supports it)

---

## Features

- Supports standard SCPI-style commands
- Automatic type inference for responses:
  - Boolean, integer, double, and string
- Array responses are transferred via shared memory buffers
- Configurable timeouts and termination characters

---

## Custom Arguments

These are optionally passed in through the custom field in the instrument config file.
**tout** : the instrument specific timeout in milliseconds. If not specified, the default timeout of the instrument-script-server will be used.
**term** : the instrument specific termination character(s). If not specified, the default termination character is \n (newline).
**arr_d** : the array delimiter character(s). If not specified, the default array delimiter is a space character. This is used to parse array responses from the instrument into data buffers.
**arg_d** : the argument delimiter character(s). If not specified, the default argument delimiter is a space character. This is used to parse command arguments sent to the instrument.

---

## Build

### Prerequisites

- CMake (>= 3.20)
- A C compiler (Clang or GCC)
- vcpkg dependencies:
  - `instrument-plugin-api`
  - `instrument-log`
  - `instrument-data`
  - `cJSON`
  - `cmocka` (for tests)

### VISA Support

- **Windows**: Requires NI-VISA installed
- **Linux**: Uses a built-in stub implementation (`stub/visa_stub.c`) if VISA is not found

---

### Build Commands

```bash
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
````

***

## Testing

Tests are implemented using `cmocka` and validate:

- Plugin metadata
- Command execution without responses
- Scalar response parsing (integer, double, boolean, string)
- Array response parsing and shared buffer creation

### Run Tests

```bash
ctest --output-on-failure
```

## Notes

- All array responses are routed through the shared buffer system
- The Linux stub is intended for development and testing only
- Real instrument communication requires a full VISA implementation (e.g. NI-VISA)

***

## License

This project is licensed under the Mozilla Public License 2.0 (MPL-2.0).

See the LICENSE file for details.
