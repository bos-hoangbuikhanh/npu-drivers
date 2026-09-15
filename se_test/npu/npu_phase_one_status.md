# NPU SE Test - Phase 1 Status

Date: 2026-09-15

## Summary

Phase 1 core command stabilization is implemented and host-validated. It is **not
DUT-qualified** and is not yet ready to claim complete Python/Pollux readiness.
No Pollux testcase was implemented.

| Command | Build | Host Checked | DUT Checked | Driver Result | Ready for Python/Pollux |
| --- | --- | --- | --- | --- | --- |
| `status` | Yes | Yes, parser only | No | No, mailbox status only | Conditional |
| `freq get` | Yes | Yes, parser only | No | Yes, signed mailbox field | Conditional |
| `freq set` | Yes | Yes, parser only | No | Yes, signed mailbox field | Conditional; separate get required |
| `thermal raw` | Yes | Yes, parser only | No | No, mask/sentinel only | Conditional |
| `thermal get` | Yes | Yes, parser only | No | No, client `ENODATA` only | Conditional |
| `reg read` | Yes | Yes, parser only | No | No, legacy completion only | Blocked for negative errno assertions |
| `reg write` | Yes | Yes, parser only | No | No, legacy completion only | Blocked until approved write target |
| `reset assert` | Yes | Yes, parser only | No | No, completion only | Conditional with recovery |
| `reset deassert` | Yes | Yes, parser only | No | No, completion only | Conditional with recovery |

## 1. Files Modified

- `src/npu_transport.h`
- `src/npu_transport.c`
- `src/npu_se_test.c`
- `Makefile`
- `NPU_PHASE1_COMMANDS.md`
- `NPU_PHASE1_STATUS.md`

No driver, Pollux, MPU/APU, or PCIe/iATU source was changed.

## 2. Transport Issues Fixed

- Register writes now wait for the source-confirmed transition where the driver
  clears `w_request` from `1` to `0`, together with mailbox READY.
- Every command clears the inactive mailbox union payload before issuing a new
  status command, preventing stale register/frequency/thermal fields from being
  interpreted as another request.
- A command checks READY first and returns `EBUSY` without overwriting an active
  mailbox transaction.
- Polling uses a monotonic two-second deadline and never retries a timed-out or
  destructive operation.
- Transport/client failures remain separate from numeric frequency driver results.
- `--json` now emits JSON for map, syntax, busy, timeout, unsupported, and other
  client errors; diagnostics remain on stderr.

## 3. Build Method

Expected developer flow:

```bash
cd /home/hoangb/BOS/bos-n1-build-script/kernel/bos-linux/tools/testing/bos/se_test/npu
make clean
make
```

The Makefile now uses `KERNEL_SRC ?= ../../../../..`, which resolves from
`se_test/npu` to the kernel root without an absolute path. It retains
`aarch64-linux-gnu-gcc`, `-Wall -Wextra -Werror`, and C11.

Host-safe validation used a separate output directory:

```bash
make clean
make CC=gcc BUILD_DIR=/tmp/npu-phase1-native
```

This completed successfully. `--help` returned exit code 0. MPU/PCIe parser checks
returned structured `ENOTSUP` without opening `/dev/mem`.

## 4. Exact Binary Path

Cross-build output:

```text
/home/hoangb/BOS/bos-n1-build-script/kernel/bos-linux/tools/testing/bos/se_test/npu/build/npu_se_test
```

The target ELF is AArch64 and dynamically linked. Confirm the deployed RootFS
contains `/lib/ld-linux-aarch64.so.1` and compatible GLIBC symbols before DUT use.
The exact deployed target path is not verified in this status report.

## 5. Commands Completed

Implemented Phase 1 commands:

- `status`
- `freq get`
- `freq set --hz <value>`
- `thermal get`
- `thermal raw`
- `reg read --addr <hex>`
- `reg write --addr <hex> --value <hex>`
- `reset assert`
- `reset deassert`

The CLI default mailbox address is `0x30000000`, matching the local endpoint IMEM
address used by the inspected driver. A PCIe host must use its own enumerated BAR
resource instead; `0x44000000` is not universally valid.

## 6. JSON Contract

In JSON mode, stdout is one JSON object and stderr is diagnostics. Exit code 0
means command success; nonzero means client/transport/driver failure. Frequency
commands expose signed numeric driver results. Register/reset/thermal commands
use `driver_result: null` because their current mailbox ABI does not publish an
errno. Timeout is reported as `ETIMEDOUT`, not as a fabricated driver result.
`freq set` reports `actual_hz: null`; run a separate `freq get` for actual rate.

## 7. Commands Tested On DUT

None. The attached DUT attempt using `--bar4 0x44000000 freq get` produced no
visible result and appeared to hang in the MMIO transaction. No DUT qualification
is claimed. Stop a hung command with `Ctrl+C`; use an externally bounded command
while investigating:

```bash
timeout 5s npu_se_test --json freq get
printf 'rc=%s\n' "$?"
```

The correct address, execution host, mailbox ownership, deployed binary/loader,
and current driver/image revision still require confirmation. Do not blindly retry
state-changing operations after a timeout.

## 8. Commands Still Limited

- Register/reset/thermal legacy mailbox paths do not publish numeric driver errno.
- Register invalid-address failures can leave the driver workqueue retrying a
  pending command; the client can only report transport timeout.
- Reset has no status query and can disrupt the Linux/NPU session.
- Thermal average is computed in the client from valid samples; individual thermal
  errors are represented by mask/sentinel, not an errno.
- `freq set` requires a separate `freq get` to observe actual rate.
- No mailbox ABI version, ownership lock, cancellation, or cross-process locking
  exists.
- Unsupported MPU/APU and PCIe/iATU commands are intentionally excluded.

## 9. Result-Propagation Gaps

The kernel handlers publish completion for successful legacy commands but leave
status/request state pending on errors. They log the real errno in dmesg, while
this userspace ABI has no result field for those commands. Fixing that completely
requires a compatible driver/mailbox ABI change, not a Python-side guess.

## 10. Phase 1 Readiness For Python/Pollux

**Partially ready for controlled Python invocation, not ready for DUT qualification.**
Python can distinguish successful frequency results, client failures, timeouts,
malformed JSON, and unsupported commands when using `--json`. It cannot currently
obtain numeric driver errno for legacy commands or guarantee hardware state
restoration. Before Pollux automation, confirm the target mailbox address and
binary runtime, perform read-only DUT checks (`status`, `freq get`, `thermal raw`,
`thermal get`, approved register read), and define recovery/cleanup ownership.

No final Python/Pollux testcase suite was implemented.
