# NPU Testcase Readiness

Audit date: 2026-09-15. Scope: source inspection and host-only build/CLI checks;
no DUT command execution and no final Pollux testcases. This report distinguishes
source-confirmed behavior from unverified board state.

## Preliminary Findings

- The current register-write CLI waits for write-request word 1 to equal 1,
  while `handle_reg_control()` clears it to 0 on success. A successful write
  therefore normally times out in this client.
- Command payloads overlap in the mailbox union. The client neither clears
  inactive register requests nor checks READY before writing. For example,
  `freq get` leaves word 4 equal to 1, which a subsequent register write can
  interpret as an additional read request. Commands are not yet safely composable.
- Only frequency commands among the requested controls publish numeric driver
  results. Legacy register/reset/MPU/iATU failures remain pending and are retried
  by the workqueue; a client timeout is not the underlying driver errno.
- `status` observes a memory word, not driver liveness or successful NPU execution.
- Inbound iATU requests are auto-aligned by the driver; expecting an alignment
  rejection would test a behavior the current implementation does not provide.

## 1. Decision And Evidence Limits

**Not ready for final SRS-qualified Pollux testcase implementation.** Python can
launch this program, but that is different from safely issuing a command,
observing the driver's actual result, proving hardware behavior, and restoring
state. Frequency replies provide the strongest current contract. Register writes
are broken at the client completion boundary. Reset, MPU, and PCIe have major
observability/restoration gaps. No command has been runtime-qualified on the DUT
in this audit. Do not run the examples as a sequence.

Inspected working trees:

| Tree | HEAD | Qualification |
| --- | --- | --- |
| Nested build kernel | `92fd05b8d4095682985f5b76a52e9393dd7c6868` | Current source; SE utility is untracked, not represented by this commit |
| Workspace-root Pollux | `3a4a34a456a203eda64908dca054b1a90b9963dc` | Execution API references below |
| Test_Automation Pollux | `fd87ddd1447a4f586945b57e1d56989eb9eb0e9c` | Different checkout; do not assume matching APIs |

No authoritative NPU SRS/VC text was located in the targeted workspace searches.
F001-F007 below refer to the requested feature descriptions, not a recovered SRS.
VC numbering in section 13 is provisional and needs requirement-owner mapping.
Previously supplied boot logs support that one board booted, not that this CLI
ran. Neither its deployed kernel revision nor its loader compatibility is proven.
The Pi address previously used for flashing identifies a flash host, not the N1
Linux endpoint. No live BDF, DUT IP, or safe scratch region is confirmed.

## 2. Source Index

Links are relative to this report. Function names identify the controlling code,
not just a wrapper or an earlier generated usage document.

| ID | Evidence |
| --- | --- |
| C | [CLI](src/npu_se_test.c): `main`, `command_reg`, `command_freq`, `command_reset`, `command_mpu`, `command_thermal`, `command_pcie` |
| T | [Transport](src/npu_transport.c), [transport constants](src/npu_transport.h), [Makefile](Makefile) |
| H | [Mailbox handlers](../../../../../drivers/misc/bos/npu_drv/egln1_npu_host.c): `handle_*`, `npu_work_function`, `npu_host_driver_init` |
| D | [NPU controls and profile tables](../../../../../drivers/misc/bos/npu_drv/egln1_npu_ctl.c): `npu_reg_ctl`, `npu_clock_*`, `npu_reset_*`, `npu_mpu_init`, `npu_atu_set`, `npu_booting_sequence` |
| A | [Private mailbox/register definitions](../../../../../drivers/misc/bos/npu_drv/egln1_npu_ctl.h), [command IDs](../../../../../drivers/misc/bos/npu_drv/egln1_npu_host.h) |
| U | [Exported build-info UAPI](../../../../../include/uapi/linux/bos_npu_mailbox.h); not a shared ABI for the controls in this CLI |
| K | [NPU Kconfig](../../../../../drivers/misc/bos/npu_drv/Kconfig), [local kernel config](../../../../../.config) |
| CLK | [A0 clock provider](../../../../../drivers/clk/bos/clk-eagle-n1-a0-evk.c), [B0 clock provider](../../../../../drivers/clk/bos/clk-eagle-n1-b0-evk.c), [BOS divider registration](../../../../../drivers/clk/bos/clk.c), [CCF divider implementation](../../../../../drivers/clk/clk-divider.c) |
| DT | [A0 EVK device tree](../../../../../arch/arm64/boot/dts/bos/eagle-n1-a0-evk.dtsi): `npu_opp_table`, `npu_devfreq`, PCIe `bar-size`/`ib-dest`, `thermal-zones` |
| DF | [NPU devfreq](../../../../../drivers/devfreq/bos-npu-devfreq.c): `bos_npu_devfreq_target`, `bos_npu_devfreq_probe` |
| EP | [Endpoint function](../../../../../drivers/pci/endpoint/functions/eagle-n1-epf.c), [DesignWare endpoint allocation](../../../../../drivers/pci/controller/dwc/pcie-designware-ep.c) |
| TM | [Thermal driver](../../../../../drivers/thermal/bos/eagle_n1_thermal.c): `tmu_get_temp` |
| P1 | [Pollux boot/login](../../../../../../../../pollux/base/utils/utils_pollux.py): `util_boot_to_stage`, `utils_ensure_login_to_linux`, `utils_expect` |
| P2 | [Pollux console/API client](../../../../../../../../pollux/base/libraries/lib_dut.py): `RemoteConsoleProxy.send_command`, `recv`, `BoardDUT.exec_command` |
| P3 | [Pollux host runner](../../../../../../../../pollux/base/libraries/lib_host.py): `host_run_stream`, `send_command`, `_resolve_dut`, `_do_close` |
| P4 | [Pollux fixtures](../../../../../../../../pollux/test_suites/conftest.py): `host`, configuration and logging |
| P5 | [Boot/dmesg tests](../../../../../../../../pollux/test_suites/n1_a0/bsp_system/test_booting.py) |
| P6 | [TMU capture and sysfs helpers](../../../../../../../../pollux/test_suites/n1_a0/bsp_system/test_tmu.py): `_read_marked_output`, `read_thermal`, `reboot_board` |
| P7 | [PCIe tests](../../../../../../../../pollux/test_suites/n1_a0/bsp_system/test_pcie.py): `check_pcie`, BAR parsing, `random_dram_rw_test` |
| P8 | [Platform reset/log examples](../../../../../../../../pollux/test_suites/n1_a0/bsp_system/test_CI_github.py): `capture_log_offsets`, `test_mainland_reset`, `test_SW_all_reset` |

## 3. Build, Deployment, And Invocation Contract

From this utility directory, exact clean and target build commands are:

```sh
make clean
make CC=aarch64-linux-gnu-gcc KERNEL_SRC=../../../../..
file build/npu_se_test
aarch64-linux-gnu-readelf -l build/npu_se_test
aarch64-linux-gnu-readelf --version-info build/npu_se_test
```

The audit ran `make -B` with the current defaults successfully. Compiler:
`aarch64-linux-gnu-gcc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0`.
Flags: `-O2 -Wall -Wextra -Werror -std=c11`. Output:
[build/npu_se_test](build/npu_se_test), AArch64 little-endian 64-bit PIE,
dynamically linked, interpreter `/lib/ld-linux-aarch64.so.1`; required symbol
versions include `GLIBC_2.17` and `GLIBC_2.34`. Verify these on the actual RootFS,
or rebuild using its SDK. No target execution was established by cross-compiling.

The Makefile's default `KERNEL_SRC=../../../..` resolves to `tools`, not the kernel
root. The override above is correct. The current build still passes because the
CLI duplicates word offsets/IDs and does not include the actual control ABI.
This is a maintenance/compatibility risk, not proof that the include path works.

The confirmed host build path is
`/home/hoangb/BOS/bos-n1-build-script/kernel/bos-linux/tools/testing/bos/se_test/npu/build/npu_se_test`.
Proposed target install path: `/usr/bin/npu_se_test`; installation and executable
permissions there remain unverified. `/bin/npu_se_test` is another previously
discussed staging destination, not a verified deployment. Use `command -v` and
`test -x` on N1, then record the absolute path. Do not rely on shell aliases.

The requested call is syntactically appropriate **only on the machine hosting
the intended physical mailbox**, with a compatible binary and PATH:

```python
import subprocess

result = subprocess.run(
  ["npu_se_test", "--json", "freq", "get"],
  capture_output=True, text=True, timeout=30,
)
```

On the Pollux controller this would run locally, not automatically on N1. The
AArch64 artifact will not execute on a typical x86 host. A native PCIe-host
build is a distinct deployment requiring that host's BAR4 resource address.

Transport prerequisites (T, H, A, K, EP, DT):

- `/dev/mem` is opened read/write even for getters. Root is the practical
  deployment requirement; device permissions, `CAP_SYS_RAWIO`, lockdown, and
  mapping policy still apply. Root alone does not guarantee access. No `sudo`
  requirement is built into the CLI; N1 previously had a root shell without it.
- Inspected config enables `CONFIG_DEVMEM`, disables `CONFIG_STRICT_DEVMEM`,
  enables `CONFIG_EAGLE_N1_NPU_HOST=y`, and disables `CONFIG_MODULES`. This is
  the build tree's config, not a measurement of the currently flashed image.
- The driver polls **local endpoint physical IMEM `0x30000000`**. DT BAR4 maps
  to that endpoint destination. The client's `0x44000000` default is a host BAR
  example, not a universally valid local N1 address. A local-N1 qualification
  would use the source-derived candidate `--bar4 0x30000000` only after confirming
  the deployed memory map and exclusive ownership. A PCIe host must enumerate
  its actual BAR4 resource; never copy another machine's BAR address.
- Explicit `--bar4` overrides `NPU_BAR4_PHYS`, but the environment is parsed first:
  an invalid environment value aborts even with a valid CLI override. Global
  options must precede the command. No other application environment is needed.
- No busy acquisition, multi-process lock, request identifier, version check,
  bounded hardware cancellation, or timeout recovery exists. Exclude all other
  mailbox clients, including host software. Python serialization alone cannot
  exclude an external PCIe writer or synchronize with an already pending request.
- MMIO mapping is page-aligned internally, but the supplied mailbox offset is
  not checked for word alignment or address validity. Volatile accesses and
  barriers do not establish cross-platform cache/device mapping correctness.

Return/output contract (C, T):

| Case | Process result | stdout with `--json` | Interpretation |
| --- | --- | --- | --- |
| Completed frequency success | 0 | Object with `driver_result: 0` | Real CCF-path result, not physical frequency measurement |
| Completed frequency failure | 1 | Object with negative `driver_result` | Actual driver errno; zero Hz produces `-22` |
| Completed legacy command | 0 | Usually object with `driver_result: null` | Completion only; no numeric result |
| Status, even busy | 0 | Object with `ready` and `mailbox_status` | Memory observation only |
| Thermal get, no valid readings | 1 | Error object, null result, average 0 | Local `-ENODATA`; 0 is not a measured temperature |
| Thermal raw, no valid readings | 0 | Raw sentinel values, zero mask | Not a successful sensor measurement |
| Parse/map/transport timeout | 1 | Generally empty | Diagnostics on stderr, not structured JSON |
| No args or `--help` | 1 | Empty | Usage on stderr; help is not a successful operation |
| Signal or Python timeout | Negative returncode or exception | May be partial/absent | Not a driver errno |

All ordinary nonzero returns collapse to exit 1. stderr includes usage even for
driver failures. JSON is one compact object for completed paths, but not for all
failures. No caller may blindly `json.loads(stdout)` or equate rc 1 to EINVAL.
Unknown commands may hit `/dev/mem` before dispatch; unsupported syntax has no
distinct exit status. Unknown/duplicate options and extra tokens are not reliably
rejected. Unsigned parsing can accept signs/whitespace; a negative 64-bit target
can become a huge unsigned address. These are blockers for unattended parameter
fuzzing. No ABI negotiation protects an older deployed driver.

The local timeout is 1000 polls with nominal 1 ms sleeps, not a monotonic 1-second
deadline. Workqueue delay is nominally 1 ms but scheduler/timer resolution matters.
Python's 30-second timeout is an outer process bound, not mailbox cancellation;
kernel/MMIO stalls and remote execution may require independent recovery.

## 4. Command Readiness

`Builds=Yes` means compiled into the target ELF, not exercised on hardware.
`Python Callable=Syntax` means argv can be constructed; all runtime prerequisites
in section 3 still apply. `Reply only` excludes transport/parse failures.

| Command | Implemented | Builds | Runtime Tested | Python Callable | JSON | Driver Result Observable |
| --- | --- | --- | --- | --- | --- | --- |
| status | Yes | Yes | No | Syntax | Reply only | No; status word only |
| reg read | Yes, stale-field risk | Yes | No | Syntax, unsafe sequences | Reply only | Value/completion, no errno |
| reg write | Yes, broken completion | Yes | No | Syntax, blocked | Usually times out | No errno |
| freq get | Yes | Yes | No | Syntax | Reply only | Yes, signed result and actual Hz |
| freq set | Yes, no automatic get | Yes | No | Syntax | Reply only | Yes, result; only requested Hz printed |
| reset assert | Yes | Yes | No | Syntax, destructive | Reply only | Completion, no errno/state |
| reset deassert | Yes | Yes | No | Syntax, destructive | Reply only | Completion, no errno/state |
| reset status | No | N/A | No | Unavailable | No | No |
| apu dump/set | No | N/A | No | Unavailable | No | No |
| mpu dump | No | N/A | No | Unavailable | No | No |
| mpu set --id | Yes | Yes | No | Syntax, destructive | Reply only | Completion, no errno/profile readback |
| thermal get | Yes | Yes | No | Syntax | Reply only | Mask and client average, no errno |
| thermal raw | Yes | Yes | No | Syntax | Reply only | Mask and readings, no per-sensor errno |
| pcie outbound | Yes | Yes | No | Syntax, destructive | Reply only | Completion, no errno/readback |
| pcie inbound | Yes | Yes | No | Syntax, destructive | Reply only | Completion, no errno/readback |
| pcie show | No | N/A | No | Unavailable | No | No |
| pcie config | Yes, raw direction | Yes | No | Syntax, destructive | Reply only | Completion, no errno/readback |

## 5. F001: Register Control

Trace: `CMD_REG_CONTROL (0xaa)` -> `handle_reg_control` -> `npu_reg_ctl` ->
prefix checks -> `readl`/`writel` -> handler completion fields (H, D, A).

Confirmed mapped ranges: SFR `0x23000000..0x2312ffff` (size `0x130000`), PRCM bus
`0x20840000..0x2084ffff`. However validation accepts the entire `0x23xxxxxx`
prefix, beyond the mapped SFR size, and does not enforce 32-bit alignment or a
register allowlist. Invalid MMIO within that broad prefix can fault/hang the
kernel. Do not use boundary fuzzing to test EINVAL.

| Input | Source evidence | Automation disposition |
| --- | --- | --- |
| Read `0x20840b48` | `RESETN_NPU_RISC`, bit 0 read by reset implementation | Source-confirmed readable candidate; fresh clean mailbox and board qualification required |
| Read `0x230d0000` | `PRCM_NPU_MUX_XPLL`; busy bit `0x100000` | Source-confirmed clock-mux read candidate, not a scratch register |
| Write `0x230d0000` | Explicit CCF-ownership guard | Returns `-EPERM` before this write; hardware register is not intrinsically read-only |
| Address `0x00000000` | Outside both accepted prefixes | Returns `-EINVAL` for an isolated request, without MMIO |
| Writable scratch address/mask | None confirmed | BLOCKED; need hardware register specification and ownership approval |
| Intrinsically read-only register | None confirmed | BLOCKED; do not relabel the mux software guard as hardware RO |

No generic write permission/mask, W1C/read-clear behavior, reserved bits, or safe
test pattern can be established from prefix validation. No safe write address
is supplied. For an approved scratch register, a future test must save the full
original word, modify only the approved mask, read back, restore, and read back
again. The broken write wait and stale union requests must first be fixed.

On error the handler logs `Register control failed` and leaves status pending.
The workqueue logs `[workqueue] NPU command 0xaa failed with ret=-22` or `-1`
and retries. The client sees local `-ETIMEDOUT`, not EINVAL/EPERM. A write can
already have occurred before an unintended/stale read fails. Dmesg can expose a
real kernel result in an isolated run, but has no transaction ID and can be
repeated/lost/interleaved. **VC3: PARTIALLY_OBSERVABLE**, not an errno-qualified
CLI test. Recovery after such a negative request is not another blind command.

## 6. F002: Frequency

`npu_clock_set` rejects zero with `-EINVAL` before any clock lookup; nonzero calls
`clk_get(NULL, "npu_clk_npu")`, `clk_round_rate`, `clk_set_rate`, then
`clk_get_rate`. Lookup, rounding, and set failures propagate through the frequency
mailbox result. `freq get` returns the CCF-reported rate in Hz (u32), not a
frequency-counter measurement (D, H, CLK).

For A0 EVK, `npu_clk_npu` aliases gate `cmu_npu_gate_npu_clk_npu`, whose parent is
the 8-bit `cmu_npu_div_npu_clk_npu` divider; its parent is `cmu_npu_mux_xpll`.
The gate propagates rate requests; the divider does not propagate them to its
parent. The generic divider permits divisors 1..256, giving CCF rates
`ceil(parent_hz / divisor)`. There is no mailbox OPP/range allowlist beyond
nonzero u32. This mathematical range is not a hardware-approved operating range.
The PLL table (2 GHz, 1.4 GHz, 1.3 GHz, 667 MHz, 500 MHz) is not the CLI's list of
settable NPU rates.

DT's A0 devfreq OPP list is 162500000, 216666667, 325000000, 650000000 Hz, with
900000/1000000 microvolt entries. Those entries assume a 1.3 GHz PLL parent.
The mailbox does not use OPP voltage sequencing; neither a DT entry nor accepting
a rate proves safe voltage/thermal conditions. Devfreq uses a userspace governor
initially and can independently change the same clock; reserve ownership and
record/restore its policy as well as the rate (DF).

**Default/current/test/rounding frequencies remain unconfirmed as runnable board
parameters.** Boot initialization leaves the mux on the oscillator, whereas a
runtime MPU set or reset deassert switches to XPLL. The previously supplied
13000000 Hz devfreq registration log is a snapshot, not a universal default.
The B0 EVK provider uses XPLL0..3 and AI/NOC/OVL clock names; the A0 aliases are
not established there. Qualify a specific silicon revision before enabling tests.

A deterministic rounding *candidate*, conditional on a measured stable 1.3 GHz
parent and approved voltage/range, is request 300000000 Hz -> 260000000 Hz
(divisor 5). With a 26 MHz parent the result differs again. Do not hardcode either
prediction until parent, mux, allowed range, and concurrent writers are checked.
Record pre-rate, request, separate `freq get`, and post-rate; the CLI's set JSON
contains only `requested_hz` and cannot itself prove requested != actual.

Exact driver messages, prefixed by `egln1_npu_ctl:` in dmesg:

```text
NPU clock request rejected: frequency is 0 Hz
NPU clock request rounded at NPU divider path: requested=%u actual=%lu
NPU clock request applied at NPU divider path: requested=%u actual=%lu
NPU clock request failed: npu_clk_npu lookup failed (%d)
NPU clock request failed: clk_round_rate(%u) returned %d
NPU clock request failed: clk_set_rate(%lu) returned %d
```

The placeholders are printf formats; match the actual requested/readback integers
within the test's new dmesg slice. Zero rejection is **AUTOMATABLE with deployment
preconditions** and should leave the rate unchanged. A genuine provider failure
is **DIFFICULT** to reproduce on a healthy correctly initialized board: no safe,
deterministic nonzero request that forces it is confirmed. Deliberately removing
the clock provider may prevent NPU boot/reset initialization altogether. Fake
errno injection or a client parse failure must not be counted as this driver VC.

## 7. F003: Reset

Runtime `reset assert` is not "hold every NPU reset asserted". It switches to
`npu_fin_pll`, waits for mux busy bit 20 to clear (nominal 1 ms poll, 10 ms limit),
asserts RISC/NOC/NPU, then deasserts NPU and NOC while retaining RISC reset.
Each reset write is checked via bit 0. Runtime `reset deassert` releases RISC,
checks it, then selects `cmu_npu_xpll` and waits for stabilization. Clock-parent
rollback is attempted if stabilization fails, but earlier reset writes are not
transactionally rolled back (D).

Source-confirmed reset register addresses: NPU `0x20840b40`, NOC `0x20840b44`,
RISC `0x20840b48`; bit 0: 0 asserted, 1 released. There is no `reset status`
mailbox command. An approved, corrected register-read path plus mux read and
`freq get` could replace state inspection; a done flag alone cannot.

**Do not use current `reg read` immediately after reset:** reset leaves union
word 1 equal to 1, which becomes an unintended register write request. This
blocks the obvious post-reset readback sequence even though addresses are known.

Proposed qualification sequence, not executable approval: quiesce all NPU work
and host DMA; record profile/rate/mux/reset state; acquire mailbox ownership;
assert; verify RISC=0, NOC=NPU=1 and stable oscillator; deassert; verify all three
bits=1 and stable XPLL; execute a board-approved known-answer NPU workload;
restore baseline policy/state. No such workload/expected result is confirmed
for this CLI. `freq get` only proves a subsequent command round trip, not NPU
compute health. Developer test apps exist but are not qualified substitutes.

Repeated reset safety and Linux-session survival remain unverified. NPU/NOC
traffic, firmware, or PCIe activity may be interrupted; keep an independent CPU
console/power recovery channel. On error the legacy handler leaves the request
pending and may repeat a partially completed sequence. **F003 VC1: BLOCKED**
for full state/functional verification; completion alone is partial evidence.

## 8. F004: APU, MPU, And Profiles

The actual command is `CMD_INIT_MPU (0xee)` -> `npu_mpu_init`. There is no
confirmed APU/MAPU/SAPU interface or equivalence in this path. Treat any SRS APU
terminology mapping as an owner decision, not a spelling alias. The CLI supports
`mpu set --id`, not `--profile`, arbitrary entry configuration, or dump (C, D, A).

| ID | Selection | Entries | Source behavior |
| --- | --- | --- | --- |
| 0 | UMD V53 | 96 | Boot default: 16 entries for each NOC2AXI0..5 |
| 1 | UMD V55 | 96 | 16 entries for each NOC2AXI0..5 |
| 2 | UMD V59 | 50 | 10 entries for NOC2AXI0, 8 each for NOC2AXI1..5 |
| 3 | Invalid | None | `Unknown MPU type: 3`, returns `-EINVAL` before table writes |

Tables live in D. NOC2AXI bases are `0x23060000` through `0x230b0000`, stride
`0x10000`; entry base stride is `0x10`. Per-entry offsets are MPU begin low/high
`0x0/0x4`, end low/high `0x8/0xc`, ATU begin low/high `0x100/0x104`, end low
`0x108`. For V53's first entry, the seven words are
`0x000000f1, 0, 0x001ff000, 0, 1, 0, 0x001ff000`. This is an example source
oracle, not a complete default-profile proof. Verify all relevant table words
and hardware permissions against an independently approved register definition.

`npu_mpu_init` writes entries but does not read them back or retain a queryable
current profile ID. After boot it also switches the mux to XPLL. A successful
done flag therefore does not prove every MPU write or MPU protection behavior.
A failure of that final mux operation can occur **after** table writes.

V59 does not clear entries omitted from its shorter table. A V53 -> V59 switch
leaves higher entries from V53 behind. Do not assume selecting an ID replaces
the entire protection configuration. Restoration to an arbitrary baseline needs
a full snapshot, including omitted entries and clock policy, not just a guessed
profile ID. A verified fresh boot establishes the source-defined V53 baseline,
but reboot is not equivalent to restoring a user's arbitrary pre-test state.

Default verification: capture real boot logs and read all table registers using
a corrected, approved readback path. Apply verification: compare the complete
affected state against the selected table and unchanged omitted entries. Invalid
verification: compare full pre/post snapshots and real driver errno. ID 3 is
source-confirmed invalid, but legacy error replies do not expose `-EINVAL` and
leave the mailbox pending, preventing safe follow-up readback in this client.
VC1 is PARTIALLY_OBSERVABLE by boot evidence; VC2/VC3 are BLOCKED for full SRS
verification/restoration. Arbitrary invalid entry configurations are unavailable.

## 9. F005: Thermal

H requests 16 zones named `npu0-thermal` through `npu15-thermal`; DT declares
these zones. Linux numeric zone indices are not fixed. Discover each by reading
`/sys/class/thermal/thermal_zone*/type`, then read its `temp` in millidegrees C.
The driver uses `thermal_zone_get_zone_by_name` and `thermal_zone_get_temp`.
It publishes all samples and the mask **before** READY; the contrary transport
comment in C is inaccurate.

Missing zones or failed reads receive `INT32_MIN` (`-2147483648`) and a cleared
valid bit. H always returns 0 after collecting them, even when every sensor
fails. TM's `tmu_get_temp` can return real `-EAGAIN` for an out-of-range raw code.
Individual errors are not returned in the mailbox; some details are only
`pr_debug`, which may not be enabled. `thermal raw` prints 16 values and a mask,
but omits sensor_count; `thermal get` prints count=16, mask, and a client-computed
integer average. There is no kernel-returned average to verify as a driver output.

A future independent check must map all zone names, preserve signed values,
compare valid bits with successful sysfs reads, and average only valid readings.
C integer division truncates toward zero, not Python's negative `//` behavior.
Use the same captured raw sample to verify arithmetic exactly; separate raw,
get, and sysfs commands are different samples. Independent sensor comparisons
need an approved time/temperature tolerance, currently unconfirmed. The existing
Pollux `read_thermal` numeric regex drops a negative sign, so it cannot be reused
unchanged for signed NPU temperature assertions.

One failed sensor should leave count=16, clear exactly its validity bit, retain
the other samples, and exclude it from the average. No non-destructive way to
force exactly one real failure is confirmed. Naturally occurring `-EAGAIN` is
**DIFFICULT**, not deterministic coverage. Disabling a DT zone requires a separate
board image and tests "missing zone", not necessarily sensor failure. Fake
injection, unsafe heating, and disabling thermal protection are not acceptable
substitutes. F005 is PARTIALLY_OBSERVABLE until the SRS average/error semantics
and tolerances are agreed.

## 10. F006: PCIe And iATU

Trace: `CMD_ATU_PROG (0xdd)` -> `handle_atu_prog` -> `npu_atu_set` -> EPC/DW
mapping operations. `bos_get_epc()`/driver-data absence returns `-ENODEV`.
Direction 0 is outbound; 1 is inbound. Raw config forwards any u32 direction,
region and u64 target. The CLI does not validate semantic ranges (C, H, D, EP).

Source DT defaults, **not host-enumerated resources**:

| BAR | Size | Endpoint destination | Notes |
| --- | --- | --- | --- |
| 0 | `0x08000000` (128 MiB) | `0x80000000` | No safe scratch ownership established |
| 2 | `0x08000000` (128 MiB) | `0x10000000` | NPU aperture; do not overwrite arbitrarily |
| 4 | `0x00200000` (2 MiB) | `0x30000000` | IMEM/mailbox destination; remapping can sever control |
| 1, 3, 5 | Reserved by default | None | Upper halves of 64-bit BARs; MSI-X option can alter layout |

DT declares 16 inbound and 16 outbound windows. A live BDF, host BAR resource
addresses, negotiated topology, enabled windows, safe inbound memory and pinned
host outbound buffer are unconfirmed. P7 shows host-side `lspci`/BAR parsing but
its random DRAM write test is not evidence of a reserved NPU scratch region.

Outbound: `region_sel` does not choose the programmed window. The code unmaps
`ep->ob_mem_phys`, then maps that allocation to the target for **SZ_1G**. DW
endpoint initialization allocates 1 GiB and initially maps it to target 0.
Allocation address is runtime-dependent. A failed remap can leave the previous
window unmapped; no rollback exists. Target 0 is a source default, not a safe
host-memory test target. A full 1 GiB pinned/reserved host target and exclusive
DMA ownership are required before traffic tests.

Inbound: `--bar` really becomes `region_sel`, then `ep->bar_to_atu[bar]`.
It is a BAR index, not an arbitrary iATU window number. Targets are floored to
`0x08000000` alignment **for every BAR**, even though BAR4 is only 2 MiB.
For example the source arithmetic maps `0x80000001` to `0x80000000`; this is
not permission to use those addresses. Unaligned requests are not rejected.
`update_ib_atu` disables the old window before programming a new one and lacks
rollback. `read_atu` indexes `bar_to_atu[region_sel]` before a BAR bounds check;
the update path also accepts a u8 bar. **Do not fuzz invalid BAR indices.**

`read_atu` is internal and returns target only, with 0 also used for read failure.
The driver logs `iATU before : 0x%llx` / `iATU after : 0x%llx`, but the CLI's
JSON echoes the requested target, not the aligned target or actual registers.
There is no `pcie show`. Full base/limit/control/enable state and traffic behavior
are not exposed; no reliable complete restore operation exists. Mapping BAR4
from a PCIe-host client can remove its own mailbox access.

| Negative scenario | Readiness | Reason |
| --- | --- | --- |
| Invalid direction (e.g. 2) | DIFFICULT; log-only observation | With EPC present, real `-EINVAL`; legacy handler leaves pending. If EPC absent, earlier `-ENODEV` wins |
| Endpoint unavailable | DIFFICULT | Requires a real topology/init condition; altering EPC may destroy transport; local IMEM could remain reachable but is unqualified |
| Configuration/provider failure | NOT_SUITABLE_FOR_POLLUX currently | No confirmed deterministic safe trigger; can disable a live window before failing |

None is READY as an errno-qualified CLI test. Do not count a userspace timeout,
an invented error response, or a rejected CLI token as a driver validation result.
Valid mappings/alignment tests are BLOCKED pending resources, readback, and recovery.

## 11. F007: Real Initialization

`module_init(npu_host_driver_init)` is the actual entry, built into the inspected
kernel. It maps IMEM/SFR/PRCM, calls `npu_booting_sequence`, and only then starts
the polling workqueue. On failure it returns the real error and unmaps resources;
no CLI "init" command exists or should be invented (H, D, K).

Inside the first boot sequence: check init flag; clear mailbox; apply V53 MPU;
switch to oscillator and perform reset-assert sequence; broadcast Tensix reset;
delay 1 ms; configure the non-TDMA Tensix clock-gating registers; release RISC
through `npu_init_reset_deassert`; set initialized flag. Boot release does **not**
call runtime `npu_reset_deassert` and does not switch back to XPLL. Later calls
to the same boot sequence with the flag set return without reinitialization.

Ordered success log evidence:

```text
[NPU Init] MPU entries initialization
MPU init request: UMD v0.53
[NPU Init] NPU reset assertion
NPU clock parent set to npu_fin_pll
[NPU Init] Broadcast NPU reset assertion
Tensix clock-gating register configuration completed
[NPU Init] NPU reset deassertion
[NPU Init] NPU initialization sequence completed successfully
[NPU Polling] init success
```

Capture a fresh boot via P1/P5 and require ordering within that boot, absence of
the corresponding initialization failures, successful Linux login, and a new
mailbox round trip. Logs prove executed checkpoints, not all register contents
or compute health. Use a real reboot/power cycle; `--skip-reboot` is inappropriate
for this VC. `rmmod`/`modprobe` are unavailable with this config, and a missing
`/proc/modules` is consistent with disabled modules, not proof `/proc` is unmounted.
F007 VC1 is READY_WITH_PRECONDITION for ordered boot evidence once deployment
and exact SRS acceptance scope are confirmed; full functional proof remains open.

## 12. Pollux Execution And Recovery

Use the workspace-root Pollux revision in section 1 as the reference checkout;
the other copy differs. Choose one explicitly before implementing tests.

| Need | Existing path/API | What is and is not established |
| --- | --- | --- |
| Boot Linux / login | P1 `util_boot_to_stage(host, cpu, request, stage="linux")`, `utils_ensure_login_to_linux` | Power cycle/login implemented; actual credentials and board binding need lab configuration |
| CPU console | P2 `host.console("console_cpu")`, `open_picocom`, `send_command`, `recv` | Remote console backed by DUT Manager HTTP; console logging supported |
| Command completion | P2 `send_command` -> `utils_expect` | Prompt match or timeout, not child exit status |
| Capture output | P6 `_snapshot_console_log`, `_read_console_log_delta`, `_read_marked_output` | Existing unique-marker/log-delta pattern; currently no preserved command rc |
| Host-side output and rc | P3 `host_run_stream` | Returns `stdout`, `rc`, `ResultStatus`; output originates from DUT API `output`, no separate stderr field |
| JSON parsing | P1 `ConfigServiceClient` response paths use `json.loads`; proposed helper below | No reusable NPU JSON-console parser found; parse framed stdout, never the full console transcript |
| dmesg | P5 boot tests, P6 TMU tests | Existing calls often check prompt only; use marker capture plus new-log boundary for assertions |
| Reset examples | P8 platform reset tests; P6 `reboot_board` | Whole-board examples, not verified NPU-only reset safety |
| Thermal | P6 `read_thermal`, `list_sys_class_items` | Reuse discovery/capture concepts; fix signed parsing and preserve rc in future helper |
| PCIe | P7 `check_pcie`, `extract_bar0_info` | Host enumeration reference, not an approved destructive mapping test |
| Cleanup | P4 `host` fixture -> P3 `sess.close`/`_do_close`; P2 console close | Closes sessions, does not restore NPU registers/clock/MPU/iATU |
| Recovery | P1 real power-cycle boot path; P3 `power_off`/`power_on` | Reacquire login and capture boot after failures; confirm independent power control in lab |

**Recommended route:** Pollux Python -> existing CPU console -> N1 shell ->
target-native CLI, with exclusive board reservation. This avoids assuming N1 SSH
is available and keeps boot logs accessible. Use the existing marker/log-offset
pattern with a unique token per invocation, explicit saved `$?`, and separated
stdout/stderr. Prefer redirecting command streams into a per-test temporary
directory, then emitting framed contents plus the saved rc; delete those files
in cleanup. Kernel printk can still interleave with serial output, so framing
must reject contamination rather than extracting arbitrary JSON-looking text.
Do not treat a prompt or an echoed command as a valid reply. These extensions
are proposed helper work, not capabilities already implemented by P6.

Direct SSH to N1 would simplify stream separation and exit status **after** its
IP/server/authentication/reconnect behavior is verified. Current P2 is an
HTTP DUT Manager client, not a general SSH-to-N1 helper. P3 chooses host or board
API routing using metadata and fastboot detection; its name does not establish
execution on N1 Linux. Never run local `/dev/mem` via `host_run_stream` without
first proving that execution host and mailbox topology are correct. Transport
timeouts must not be reported as remote command failures with fabricated errno.

Suggested future location: a new NPU module in the existing
[A0 BSP system directory](../../../../../../../../pollux/test_suites/n1_a0/bsp_system).
Only after B0 clock/address/SRS qualification, use its existing
[B0 BSP system directory](../../../../../../../../pollux/test_suites/n1_b0/bsp/system).
No final testcase or reusable Pollux module is added by this audit.

## 13. Feasibility Matrix

Statuses describe current source feasibility, not a hardware PASS. Every row
requiring CLI execution inherits the deployment/ownership gates in section 3.
No authoritative VC wording was found: the "working interpretation" column
maps the request's descriptions provisionally and must be reconciled with SRS.

| Feature/VC | Working interpretation, not SRS quotation | Status | Decisive gap/precondition |
| --- | --- | --- | --- |
| F001 VC1 | Valid register read | READY_WITH_PRECONDITION | Approved readable address, fresh clean mailbox, deployment/ownership qualification |
| F001 VC2 | Write/readback/restore | BLOCKED | Broken write completion, stale flags, no approved scratch mask |
| F001 VC3 | Invalid/protected register errors | PARTIALLY_OBSERVABLE | Real errno in dmesg only; pending request recovery unresolved |
| F002 VC1 | Get/set nonzero frequency | READY_WITH_PRECONDITION | Approved rate/voltage, stable provider, separate get and restore |
| F002 VC2 | Rounding plus message | BLOCKED | No board-confirmed deterministic safe request/actual pair |
| F002 VC3 | Zero/provider error | DIFFICULT | Zero rejection is automatable; real provider-failure trigger unconfirmed |
| F003 VC1 | Reset completion/state/function | BLOCKED | No state command; unsafe current register follow-up; workload/recovery unqualified |
| F004 VC1 | Default APU/MPU configuration | PARTIALLY_OBSERVABLE | V53 boot logs available; full readback and APU terminology mapping absent |
| F004 VC2 | Apply valid configuration | BLOCKED | No full dump/restore; shorter V59 leaves entries; clock side effect |
| F004 VC3 | Invalid and unchanged configuration | BLOCKED | Real EINVAL not returned; pending mailbox prevents trustworthy post-check |
| F005 VC1 | Thermal readings/average/failure | PARTIALLY_OBSERVABLE | Average is userspace; independent tolerance and real one-sensor failure unqualified |
| F006 VC1 | Outbound mapping | BLOCKED | Safe 1 GiB target, actual readback/restore absent; region argument not selecting window |
| F006 VC2 | Inbound mapping | BLOCKED | Approved BAR/target, bounds safety, snapshot/restore absent |
| F006 VC3 | Inbound alignment | BLOCKED | Auto-align behavior known; safe board parameters and actual mapping readback absent |
| F006 VC4 | Invalid/unavailable/config error | DIFFICULT | Log-only errors; config failure NOT_SUITABLE_FOR_POLLUX without safe trigger/recovery |
| F007 VC1 | Real boot initialization | READY_WITH_PRECONDITION | Fresh boot/log ordering, exact SRS scope, known image; no synthetic init command |

## 14. Cleanup Matrix And Central Configuration

| Changed state | Before | Required cleanup/verification | Current readiness |
| --- | --- | --- | --- |
| Register | Approved full original value and writable mask | Restore original allowed bits, read back, check no side effects | BLOCKED: no scratch permission and broken write path |
| Frequency | Actual rate, parent/mux, governor/policy, ownership | Set prior rate, get and compare; restore policy; parent changes need separate recovery | Conditional; rate-only restore cannot undo reset/MPU parent changes |
| Reset | Reset bits, workload state, rate/parent/profile | Restore approved reset state and clock policy, rerun known-answer workload | BLOCKED; partial reset can persist, reboot restores only qualified boot baseline |
| MPU/APU | All relevant registers, including omitted V59 entries, plus clock state | Restore full snapshot and verify; profile ID alone insufficient | BLOCKED; no dump/generic safe restore |
| PCIe | All inbound/outbound base/limit/target/control/enable states and ownership | Restore mapping/windows and verify traffic; keep out-of-band console | BLOCKED; mapping calls can destroy old window before failure |
| Thermal | Read-only sampling | No configuration cleanup; remove temporary capture files | Read-only, subject to mailbox ownership |
| Mailbox after timeout | Request/command and new dmesg evidence | Stop further requests; preserve logs; use qualified board reboot if pending | No in-band cancel/reset API; do not zero status blindly |

A timeout is an uncertain-effect event. Never automatically retry writes, resets,
profile changes, or iATU remaps. Capture evidence first; a failing test must still
report cleanup failure separately. Reboot can recover a known boot baseline but
must not be advertised as restoring arbitrary prior mappings or workload state.

Central lab configuration template: empty means **UNCONFIRMED**, not zero or
permission to choose an arbitrary value. Populated values below are explicitly
source-qualified only. This block is not authorization to run hardware tests.

```sh
NPU_TEST_BINARY=
NPU_VALID_READ_ADDR=0x20840b48
NPU_VALID_WRITE_ADDR=
NPU_VALID_WRITE_MASK=
NPU_INVALID_ADDR=0x00000000
NPU_READ_ONLY_ADDR=
NPU_DEFAULT_FREQ=
NPU_TEST_FREQ=
NPU_ROUNDING_TEST_FREQ=
NPU_DEFAULT_APU_PROFILE=
NPU_VALID_APU_PROFILE=
NPU_INVALID_APU_ID=
NPU_PCIE_OUTBOUND_REGION=
NPU_PCIE_OUTBOUND_TARGET=
NPU_PCIE_INBOUND_BAR=
NPU_PCIE_BAR_SIZE=
NPU_PCIE_ALIGNED_TARGET=
NPU_PCIE_UNALIGNED_TARGET=
NPU_BAR4_PHYS=
NPU_WRITE_PROTECTED_ADDR=0x230d0000
NPU_DEFAULT_MPU_PROFILE=0
NPU_VALID_MPU_PROFILE=1
NPU_INVALID_MPU_ID=3
```

Read address is the source-confirmed RISC reset bit register, not a runtime-tested
general scratch location. Invalid address exercises prefix rejection only with
isolated flags. Protected mux address is software write-protected, not hardware
read-only. APU fields deliberately remain empty because MPU equivalence is not
confirmed. MPU IDs are source-defined selections, not safety-qualified profiles.
BAR size remains empty until a particular live BAR is selected, despite the DT
defaults in section 10. Proposed binary path is `/usr/bin/npu_se_test`; local
mailbox candidate is `0x30000000`, both requiring target deployment qualification.

## 15. Proposed Python Normalization Helper And Examples

This documentation-only helper runs **locally on the chosen execution machine**.
It executes, captures, parses, and normalizes; it does not implement register,
clock, reset, sensor, or PCIe logic, and it never retries or repairs the mailbox.
Require explicit deployment variables to avoid accidentally using the host BAR
example. A Pollux console adapter must supply the same captured fields separately;
the helper is not itself a remote-execution transport.

```python
import json
import os
import subprocess

COMMANDS = {
  ("status",): "status",
  ("reg", "read"): "reg_read",
  ("reg", "write"): "reg_write",
  ("freq", "get"): "freq_get",
  ("freq", "set"): "freq_set",
  ("reset", "assert"): "reset_assert",
  ("reset", "deassert"): "reset_deassert",
  ("mpu", "set"): "mpu_set",
  ("thermal", "get"): "thermal_get",
  ("thermal", "raw"): "thermal_raw",
  ("pcie", "outbound"): "pcie_config",
  ("pcie", "inbound"): "pcie_config",
  ("pcie", "config"): "pcie_config",
}


def run_npu_command(args, timeout=30):
  if isinstance(args, (str, bytes)):
    raise TypeError("args must be an argv sequence")
  args = [str(value) for value in args]
  key = tuple(args[:1] if args[:1] == ["status"] else args[:2])
  record = {"outcome": None, "returncode": None, "stdout": "",
        "stderr": "", "data": None, "driver_result": None}
  expected = COMMANDS.get(key)
  if expected is None:
    return dict(record, outcome="command_unavailable")
  binary = os.environ.get("NPU_TEST_BINARY")
  mailbox = os.environ.get("NPU_BAR4_PHYS")
  if not binary or not mailbox:
    return dict(record, outcome="setup_error",
          stderr="Set qualified NPU_TEST_BINARY and NPU_BAR4_PHYS")
  argv = [binary, "--json", "--bar4", mailbox, *args]
  try:
    process = subprocess.run(argv, capture_output=True, text=True,
                 timeout=timeout, check=False)
  except FileNotFoundError as error:
    return dict(record, outcome="command_unavailable", stderr=str(error))
  except subprocess.TimeoutExpired as error:
    def as_text(value):
      return value.decode(errors="replace") if isinstance(value, bytes) else value or ""
    return dict(record, outcome="process_timeout", stdout=as_text(error.stdout),
          stderr=as_text(error.stderr))
  except (OSError, UnicodeError) as error:
    return dict(record, outcome="execution_error", stderr=str(error))
  record.update(returncode=process.returncode, stdout=process.stdout,
          stderr=process.stderr)
  if process.returncode < 0:
    return dict(record, outcome="process_signal")
  if not process.stdout.strip():
    outcome = "client_failure_no_json" if process.returncode else "missing_json"
    if process.returncode and "(-110)" in process.stderr:
      outcome = "client_timeout"
    return dict(record, outcome=outcome)
  try:
    data = json.loads(process.stdout)
  except json.JSONDecodeError:
    return dict(record, outcome="malformed_json")
  record["data"] = data
  if not isinstance(data, dict) or data.get("command") != expected:
    return dict(record, outcome="contract_error")
  if data.get("result") not in ("ok", "error"):
    return dict(record, outcome="contract_error")
  if (process.returncode == 0) != (data["result"] == "ok"):
    return dict(record, outcome="contract_error")
  driver_result = data.get("driver_result")
  record["driver_result"] = driver_result
  if driver_result is not None:
    if type(driver_result) is not int or driver_result > 0:
      return dict(record, outcome="contract_error")
    if (driver_result == 0) != (process.returncode == 0):
      return dict(record, outcome="contract_error")
    return dict(record, outcome="driver_success" if driver_result == 0 else "driver_failure")
  if expected in ("freq_get", "freq_set"):
    return dict(record, outcome="contract_error")
  if process.returncode:
    return dict(record, outcome="client_failure")
  return dict(record, outcome="status_observation" if expected == "status" else "completed_unverified")
```

`driver_failure` is reserved for a signed mailbox result, not parsed dmesg.
`client_timeout` identifies the current client's stderr marker only; it cannot
identify the original driver error or prove cancellation. `command_unavailable`
includes unsupported verbs and missing executable/interpreter; Linux ENOENT may
mean a missing ELF loader even when the file exists. The helper deliberately
does not guess why empty JSON failed. Command-specific data validation, safety
approval, and restoration belong to future tests, not this execution helper.

Every implemented command is represented below as Python argv. The catalogue
is **not a sequence to execute**, and contains no invented safe target/value.
For a single approved invocation, call
`run_npu_command(command_examples["freq_get"], timeout=30)`. For setters replace
each placeholder from qualified lab config and a captured pre-state. Do not pass
the placeholder strings to the CLI.

```python
command_examples = {
  "status": ["status"],
  "reg_read": ["reg", "read", "--addr", "<qualified-read-address>"],
  "reg_write": ["reg", "write", "--addr", "<approved-write-address>",
          "--value", "<masked-value-from-saved-original>"],
  "freq_get": ["freq", "get"],
  "freq_set": ["freq", "set", "--hz", "<approved-frequency-hz>"],
  "freq_zero": ["freq", "set", "--hz", "0"],
  "reset_assert": ["reset", "assert"],
  "reset_deassert": ["reset", "deassert"],
  "mpu_set": ["mpu", "set", "--id", "<approved-mpu-id>"],
  "thermal_get": ["thermal", "get"],
  "thermal_raw": ["thermal", "raw"],
  "pcie_outbound": ["pcie", "outbound", "--region", "<qualified-region>",
            "--target", "<reserved-host-target>"],
  "pcie_inbound": ["pcie", "inbound", "--bar", "<approved-bar>",
           "--target", "<approved-endpoint-target>"],
  "pcie_config": ["pcie", "config", "--direction", "<0-or-1>",
          "--region", "<region-or-bar>", "--target", "<approved-target>"],
}
```

For readback after an approved frequency set, issue a separate get and require
`driver_success` for both; preserve the original rate for cleanup. To test a
confirmed negative frequency response, zero Hz should return exit 1 with
`driver_result == -22`. For MPU invalid-ID syntax substitute 3 only in a separately
qualified recovery test; expect the current client to time out, not expose -22.
For raw PCIe invalid-direction syntax substitute 2 only with a qualified endpoint
and recovery plan. No examples imply that `reset status`, `mpu dump`, `apu set`,
or `pcie show` currently exist.

## 16. Validation And Required Follow-Up

Completed audit checks:

- Current CLI target build passed with warnings treated as errors; ELF/loader and
  GLIBC symbol requirements inspected. Target build artifact remains AArch64.
- Separate native build passed in a temporary directory. Python subprocess checks
  for no arguments, `--help`, invalid `--bar4`, and invalid `NPU_BAR4_PHYS` all
  returned 1, empty stdout, and diagnostics on stderr. No physical memory was
  accessed by these checks. The native map-failure invocation was **skipped**
  because `/dev/mem` exists on the host; it was not safe to probe the default map.
- Profile counts were checked directly from source initializers: 96/96/50.
- All three report Python blocks passed syntax checks; all 31 local links
  resolved. Eighteen host-only helper checks passed, covering signed driver
  results, null results, malformed/missing JSON, inconsistent contracts, process
  timeout/signal, client timeout, missing executable, permissions, unsupported
  command, and missing setup. All 16 VC rows and every implemented command's
  example were checked for presence. These are normalization/document checks,
  not driver or DUT runtime testing.

Before final test implementation, resolve these gates in order:

1. Obtain exact SRS/VC text and bind the intended silicon, kernel, DT, RootFS,
   Pollux checkout, binary path, and execution host. Verify mailbox physical
   address, loader, root access, sole ownership, independent recovery, and one
   real read-only command round trip.
2. Correct client write completion, inactive union fields, ownership/busy handling,
   strict syntax, monotonic timeout behavior, and structured JSON for all outcomes.
   Add frequency readback to the contract or explicitly require a separate get.
   Define signal/timeout uncertain-effect handling and help/unsupported behavior.
  Validate thermal count/mask/sentinel consistency and represent unavailable
  averages explicitly rather than reporting a misleading zero measurement.
3. Agree a compatible driver protocol for real result/done publication on legacy
   commands and terminal failure completion without indefinite retries. Add
   versioning/readback where SRS requires it; do not fabricate driver results
   inside Python. Review register mapping bounds and inbound BAR validation.
4. Approve register write mask, rates/voltage/parent, MPU semantic mapping and
   full-state restore, reset workload, PCIe reserved targets/restore, and thermal
   tolerance/failure scenario. Resolve V59 retained entries and iATU rollback.
5. Qualify Pollux's framed output plus rc capture and dmesg boundaries, then run
   controlled board experiments with cleanup evidence before adding final tests.

**Final answer to the readiness question:** Python can parameterize every
implemented command, but cannot currently obtain an actual numeric driver result
for each, prove all requested SRS outcomes, or reliably restore every changed
state. A buildable CLI is available; a safe, complete Pollux qualification contract
is not. No driver, CLI, build wrapper, or final Pollux testcase was changed by
this readiness investigation.
