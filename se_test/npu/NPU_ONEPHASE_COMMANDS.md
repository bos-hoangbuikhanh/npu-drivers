# NPU SE Test - Phase 1 Commands

Phase 1 contains only status, frequency, thermal, register, and reset commands.
MPU/APU and PCIe/iATU commands are intentionally out of scope.

## Build

```bash
cd /home/hoangb/BOS/bos-n1-build-script/kernel/bos-linux/tools/testing/bos/se_test/npu
make clean
make
```

The default compiler is `aarch64-linux-gnu-gcc`, with `-Wall -Wextra -Werror`.
The binary is `build/npu_se_test`. Override `CC` for a host-only parser build,
for example `make clean && make CC=gcc BUILD_DIR=/tmp/npu-se-test-native`.

The default mailbox address is the local endpoint IMEM address `0x30000000`.
Use `--bar4 <hex>` only with a confirmed physical address for the execution host,
or set `NPU_BAR4_PHYS`. `/dev/mem` access requires appropriate root/device
permissions. Never use an unconfirmed address on a DUT.

## Common JSON Contract

With `--json`, stdout contains one JSON object only and diagnostics go to stderr.
Exit status is zero only for a successful command. Numeric driver results are
preserved where the mailbox ABI supplies them; otherwise `driver_result` is null.
Transport/client errors also return nonzero and use `driver_result: null`.

Example driver failure:

```json
{"command":"freq_set","result":"error","driver_result":-22,"error":"EINVAL"}
```

A timeout is a transport/client failure, not a fabricated driver errno. The
client uses a deterministic monotonic deadline and does not retry a timed-out
operation.

## Status

```bash
npu_se_test status
npu_se_test --json status
```

Arguments: none. Example JSON:

```json
{"command":"status","result":"ok","mailbox_status":0,"ready":true,"driver_result":null}
```

Mapping: reads the mailbox status word only; no driver command is sent. Numeric
driver result: unavailable. Limitation: `ready` means the observed mailbox word
is READY, not that NPU hardware or firmware is healthy. Safety: read-only, but
mapping still requires the correct mailbox address.

## Frequency

```bash
npu_se_test freq get
npu_se_test freq set --hz <hz>

npu_se_test --json freq get
npu_se_test --json freq set --hz <hz>
```

`--hz` is a nonnegative 32-bit value. The value is forwarded to
`CMD_SET_NPU_FREQ -> handle_set_npu_freq() -> npu_clock_set()`, which performs
clock lookup, rounding, setting, and readback in the driver. `freq get` maps to
`CMD_GET_NPU_FREQ -> npu_clock_get()`.

Example get JSON:

```json
{"command":"freq_get","result":"ok","driver_result":0,"actual_hz":650000000}
```

Example set JSON when successful:

```json
{"command":"freq_set","result":"ok","driver_result":0,"requested_hz":300000000,"actual_hz":null}
```

The current set mailbox ABI does not publish actual frequency, so `actual_hz`
is explicitly null. Run a separate `freq get` for actual readback. Numeric driver
result: yes, signed mailbox result. `--hz 0` reaches the driver and normally
returns its real `-EINVAL`. Safety: changing frequency affects shared clock state;
record the original value and restore it with a qualified value. Do not infer a
safe rate from this syntax alone.

## Thermal

```bash
npu_se_test thermal raw
npu_se_test thermal get

npu_se_test --json thermal raw
npu_se_test --json thermal get
```

Arguments: none. Mapping: `CMD_GET_THERMAL -> handle_get_thermal()`, which reads
up to 16 zones named `npu0-thermal` through `npu15-thermal`. Invalid sensors are
represented by the signed sentinel `-2147483648` and excluded by `valid_mask`.
The driver has no numeric result field and returns READY after publishing data.

Example raw JSON:

```json
{"command":"thermal_raw","result":"ok","driver_result":null,"sensor_count":16,"valid_mask":"0xffff","temperatures_mdegc":[50000,51000]}
```

The actual output contains all 16 readings. Example get JSON:

```json
{"command":"thermal_get","result":"ok","driver_result":null,"sensor_count":16,"valid_count":15,"valid_mask":"0xfffe","average_mdegc":52333,"average_source":"client"}
```

Numeric driver result: unavailable; individual zone failures are represented only
by the mask/sentinel. If no sensor is valid, `thermal get` returns an `ENODATA`
client error and never reports zero as a temperature. Safety: read-only. Sensor
availability and units must be confirmed on the deployed DT; do not treat missing
zones as a driver failure without checking the mask.

## Register

```bash
npu_se_test reg read --addr <hex>
npu_se_test reg write --addr <hex> --value <hex>

npu_se_test --json reg read --addr <hex>
npu_se_test --json reg write --addr <hex> --value <hex>
```

Arguments are 32-bit hexadecimal addresses/values. Mapping:
`CMD_REG_CONTROL -> handle_reg_control() -> npu_reg_ctl()`. Example read:

```bash
npu_se_test --json reg read --addr 0x20840b48
```

```json
{"command":"reg_read","result":"ok","driver_result":null,"address":"0x20840b48","value":"0x00000001"}
```

Write example, requiring an approved address and mask:

```bash
npu_se_test --json reg write --addr <approved-address> --value <approved-value>
```

```json
{"command":"reg_write","result":"ok","driver_result":null,"address":"0x00000000","value":"0x00000000"}
```

The example values are placeholders and must not be passed as a test. Numeric
driver result: unavailable in the current mailbox ABI. The client now clears both
register request fields before each transaction and waits for the actual write
transition, request `1` to `0`, plus READY. Invalid addresses are allowed through
syntax validation so the driver can validate them, but the legacy driver failure
path leaves the mailbox pending and the client can only report a timeout; it must
not invent `EINVAL`/`EPERM`. Safety: reads require approved addresses; writes
require an owner-approved writable register, bit mask, saved original value,
readback, and restoration. No default write address/value exists.

## Reset

```bash
npu_se_test reset assert
npu_se_test reset deassert

npu_se_test --json reset assert
npu_se_test --json reset deassert
```

Arguments: none. Mapping: `CMD_ASSERT_RESET -> npu_reset_assert()` and
`CMD_DEASSERT_RESET -> npu_reset_deassert()`. Example JSON:

```json
{"command":"reset_assert","result":"ok","driver_result":null}
```

Numeric driver result: unavailable in the current mailbox ABI. The command waits
for the driver completion flag and READY; no artificial stabilization sleep is
added in userspace. Limitation: there is no reset-status command, and a driver
failure can remain pending until timeout. Safety: reset can interrupt NPU work and
clock state; use an independent recovery path and only run after a qualified
baseline/restore procedure.

## Unsupported In Phase 1

`mpu`, `apu`, `pcie`, `iatu`, and related dump/show/configuration commands are not
implemented in this phase. They return a JSON `ENOTSUP` client error without
opening the mailbox. No Pollux testcase is included.
