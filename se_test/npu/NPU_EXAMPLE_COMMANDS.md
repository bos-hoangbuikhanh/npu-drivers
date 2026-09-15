# NPU SE Test - Real Phase 1 Example Commands

This file maps source-confirmed commands for the current Phase 1 CLI. It does not
modify the driver or Pollux and does not claim DUT qualification. Commands marked
as candidates still require the deployed image, silicon revision, mailbox
ownership, and recovery procedure to be confirmed on the DUT.

Source basis:

- CLI and mailbox transport: `tools/testing/bos/se_test/npu/src/npu_se_test.c`,
  `src/npu_transport.c`, and `src/npu_transport.h`.
- Driver control path: `drivers/misc/bos/npu_drv/egln1_npu_host.c` and
  `egln1_npu_ctl.c`/`.h`.
- A0 EVK clock/OPP/endpoint/thermal data:
  `arch/arm64/boot/dts/bos/eagle-n1-a0-evk.dtsi`.

## 1. Execution Command

For the current locally installed AArch64 binary on N1 Linux:

```bash
./npu_se_test --json status
```

The current CLI default is `0x30000000`, so `--bar4` is not required when the
process is running on N1 Linux and the deployed driver/mailbox use the source
mapping:

```c
#define PHYS_ADDR_IMEM_SRAM 0x30000000
```

The explicit equivalent is:

```bash
./npu_se_test --bar4 0x30000000 --json status
```

`0x30000000` is the endpoint's local IMEM physical address, not a universally
valid PCIe host BAR resource. A host-side process must use that host's enumerated
BAR address instead. The previously tried `0x44000000` is not source-confirmed
for local N1 Linux and must not be used as a default.

The binary must be the AArch64 build and executable on the DUT. Verify before
running:

```bash
file ./npu_se_test
command -v ./npu_se_test
```

The process needs root-equivalent `/dev/mem` access. Use an external bound while
investigating a mapping or mailbox problem:

```bash
timeout 5s ./npu_se_test --json status
printf 'rc=%s\n' "$?"
```

A timeout is not evidence of a driver errno. Do not retry state-changing commands
after an uncertain timeout without recovery.

## 2. Command Classification

| Classification | Meaning |
| --- | --- |
| `READ_ONLY_SAFE_CANDIDATE` | Does not intentionally change NPU state, but still needs correct mailbox mapping and ownership |
| `NEGATIVE_SAFE_CANDIDATE` | Intended rejection before functional state change; verify actual response/logs |
| `STATE_CHANGING_REQUIRES_RESTORE` | Changes shared state and needs a measured before/after restore |
| `DESTRUCTIVE_REQUIRES_RECOVERY` | Can interrupt NPU/Linux operation or leave the mailbox pending |
| `BLOCKED_NO_SAFE_PARAMETER` | No source-confirmed safe concrete parameter exists |

"Source-confirmed" below means the value/function is present in this source tree;
"DUT-qualified" means actually exercised successfully on the deployed board. No
DUT qualification was available when this file was created.

## 3. Status

Command:

```bash
./npu_se_test --json status
```

Classification: `READ_ONLY_SAFE_CANDIDATE`.

Mapping: local mailbox word 0 only. It does not issue a driver command and cannot
prove NPU hardware health, firmware execution, or driver liveness.

Expected shape from the current CLI:

```json
{"result":"ok","command":"status","mailbox_status":0,"ready":true,"driver_result":null}
```

Concrete parameter: none. Source-confirmed: yes. DUT-qualified: no.

## 4. Frequency

### Frequency get

```bash
./npu_se_test --json freq get
```

Classification: `READ_ONLY_SAFE_CANDIDATE`.

Mapping:

```text
CMD_GET_NPU_FREQ (0x45)
  -> handle_get_npu_freq()
  -> npu_clock_get()
  -> clk_get(NULL, "npu_clk_npu")
  -> clk_get_rate()
```

The numeric driver result and CCF-reported `actual_hz` are returned:

```json
{"result":"ok","command":"freq_get","driver_result":0,"actual_hz":650000000}
```

The number above is an output example, not a claimed DUT value. Source-confirmed:
command path and Hz units. DUT-qualified: no.

### A0 frequency candidates

The A0 EVK `npu_opp_table` contains these rates:

| Rate | Source status | Classification |
| ---: | --- | --- |
| `162500000` Hz | A0 DT OPP, 1.3 GHz / 8 | `STATE_CHANGING_REQUIRES_RESTORE`, candidate requiring DUT qualification |
| `216666667` Hz | A0 DT OPP, 1.3 GHz / 6 | `STATE_CHANGING_REQUIRES_RESTORE`, candidate requiring DUT qualification |
| `325000000` Hz | A0 DT OPP, 1.3 GHz / 4 | `STATE_CHANGING_REQUIRES_RESTORE`, candidate requiring DUT qualification |
| `650000000` Hz | A0 DT OPP, 1.3 GHz / 2 | `STATE_CHANGING_REQUIRES_RESTORE`, candidate requiring DUT qualification |

These are source-supported A0 devfreq OPP values, not proof that the mailbox's
direct `npu_clock_set()` path enforces the OPP table or that another silicon/DT
variant supports them. The driver accepts any nonzero 32-bit request, calls
`clk_round_rate()`, then `clk_set_rate()`. Record the original `freq get` result,
set one approved candidate, issue a separate `freq get`, and restore the original
rate. The current `freq set` response has `actual_hz: null`; only `freq get`
provides the actual readback.

Concrete A0 candidate sequence, not DUT approval:

```bash
./npu_se_test --json freq get
./npu_se_test --json freq set --hz 325000000
./npu_se_test --json freq get
```

Other source-supported A0 candidates:

```bash
./npu_se_test --json freq set --hz 162500000
./npu_se_test --json freq set --hz 216666667
./npu_se_test --json freq set --hz 650000000
```

Do not run all candidates as a sequence without restoring between them. Source
status: confirmed A0 DT candidates. DUT status: unqualified.

### Negative zero-frequency case

```bash
./npu_se_test --json freq set --hz 0
./npu_se_test --json freq get
```

Classification: first command `NEGATIVE_SAFE_CANDIDATE`; second command
`READ_ONLY_SAFE_CANDIDATE`.

`npu_clock_set()` rejects zero before clock lookup or rate mutation with
`-EINVAL`, and the frequency mailbox publishes that numeric result. Expected
shape:

```json
{"result":"error","command":"freq_set","driver_result":-22,"requested_hz":0,"actual_hz":null}
```

The following get checks that the rate remains observable. This is source-confirmed
behavior, not DUT qualification.

## 5. Register Read Examples

`npu_reg_ctl()` accepts addresses with either:

- PRCM bus prefix `0x2084xxxx`, mapped from physical base `0x20840000`; or
- SFR prefix `0x23xxxxxx`, mapped from physical base `0x23000000` with size
  `0x130000`.

The validation is prefix-based rather than a complete register allowlist. Only
addresses below are source-confirmed register definitions or direct source uses.
Do not turn the broad accepted prefix into a fuzz range.

| Address | Register | Meaning | Read safety |
| --- | --- | --- | --- |
| `0x20840b40` | `RESETN_NPU` | PMD NPU reset control; bit 0 is checked by `npu_reset_assert()` | `READ_ONLY_SAFE_CANDIDATE`; read has no source-defined side effect, DUT unqualified |
| `0x20840b44` | `RESETN_NPU_NOC` | PMD NOC reset control; bit 0 is checked by reset code | `READ_ONLY_SAFE_CANDIDATE`; DUT unqualified |
| `0x20840b48` | `RESETN_NPU_RISC` | PMD RISC reset control; bit 0 is checked by reset code | `READ_ONLY_SAFE_CANDIDATE`; DUT unqualified |
| `0x230d0000` | `PRCM_NPU_MUX_XPLL` | NPU clock mux/status register; bit `0x100000` is polled for stabilization | `READ_ONLY_SAFE_CANDIDATE`; clock state is observable, DUT unqualified |

Concrete read commands:

```bash
./npu_se_test --json reg read --addr 0x20840b40
./npu_se_test --json reg read --addr 0x20840b44
./npu_se_test --json reg read --addr 0x20840b48
./npu_se_test --json reg read --addr 0x230d0000
```

Expected successful shape:

```json
{"result":"ok","command":"reg_read","driver_result":null,"address":"0x20840b48","value":"0x00000001"}
```

The value is a DUT result and must not be hardcoded in an assertion until the
board state is known. The current mailbox ABI does not return `npu_reg_ctl()`'s
numeric errno. A driver-side rejection leaves the request pending; the CLI can
report only a transport timeout and dmesg may contain the real errno.

## 6. Invalid Register Example

Concrete source-confirmed negative candidate:

```bash
./npu_se_test --json reg read --addr 0x00000000
```

Classification: `NEGATIVE_SAFE_CANDIDATE`.

Trace:

```text
CMD_REG_CONTROL (0xaa)
  -> handle_reg_control()
  -> npu_reg_ctl()
  -> is_sfr_rel_npu_addr() = false
  -> is_prcm_bus_addr() = false
  -> -EINVAL before MMIO access
```

This address is outside both accepted prefixes, so it is preferred over an address
inside `0x23xxxxxx` that could pass the broad validation and access hardware. The
current legacy mailbox does not publish the errno on failure, so the observed CLI
result may be a client `ETIMEDOUT`, not `driver_result: -22`. Do not mislabel that
timeout as proof of `EINVAL`; inspect the kernel log and mailbox behavior.

## 7. Register Permission Example

The driver explicitly protects the clock mux address from direct writes:

```c
if (address == PHYS_ADDR_REG_SFR + PRCM_NPU_MUX_XPLL)
    return -EPERM;
```

Concrete negative candidate:

```bash
./npu_se_test --json reg write \
    --addr 0x230d0000 \
    --value 0x00000000
```

Classification: `NEGATIVE_SAFE_CANDIDATE`.

Source expectation: `npu_reg_ctl()` returns `-EPERM` before `write_u32()` for this
address. The current mailbox handler does not publish that errno and leaves the
command pending on failure. Therefore the CLI may return a transport timeout and
`driver_result` remains null; it must not claim that the JSON timeout is `EPERM`.
This is the only source-confirmed write rejection in the inspected path.

## 8. Valid Register Write

```text
NO SOURCE-CONFIRMED SAFE REGISTER WRITE TARGET
```

The driver allows writes to any address matching the broad `0x23xxxxxx` or
`0x2084xxxx` prefixes except the clock mux write above. The inspected source does
not provide a writable scratch register, complete writable mask, side-effect
classification, or restoration contract for any such address. Do not run a
`reg write` with a guessed address, reset register, clock register, reserved bit,
or arbitrary value.

The CLI transaction bug is fixed, but that does not make a hardware register safe:
valid write qualification still requires an approved address/mask, saved original
word, readback, and restoration plan.

## 9. Thermal Examples

```bash
./npu_se_test --json thermal raw
./npu_se_test --json thermal get
```

Classification: `READ_ONLY_SAFE_CANDIDATE` for both.

Mapping:

```text
CMD_GET_THERMAL (0xcc)
  -> handle_get_thermal()
  -> thermal_zone_get_zone_by_name("npu0-thermal" ... "npu15-thermal")
  -> thermal_zone_get_temp()
```

Current raw JSON fields:

- `sensor_count`: driver-published count, normally 16 in this source path.
- `valid_mask`: one bit per successfully read sensor.
- `temperatures_mdegc`: all 16 signed readings; invalid readings are
  `-2147483648` and must not be treated as temperatures.
- `driver_result`: null because the thermal mailbox has no result field.

Example shape:

```json
{"result":"ok","command":"thermal_raw","driver_result":null,"sensor_count":16,"valid_mask":"0xffff","temperatures_mdegc":[50000,51000,52000,53000,54000,55000,56000,57000,58000,59000,60000,61000,62000,63000,64000,65000]}
```

The values above are illustrative output shape, not source/DUT measurements.

`thermal get` computes its average in userspace from valid entries only:

```json
{"result":"ok","command":"thermal_get","driver_result":null,"sensor_count":16,"valid_count":15,"valid_mask":"0xfffe","average_mdegc":52333,"average_source":"client"}
```

The numbers above are illustrative. If no bit is valid, the CLI returns a client
`ENODATA` error and does not report zero as an average. The driver itself returns
READY even if individual zones are missing or failed, so the mask is essential.
The A0 DT declares the NPU thermal zones, but runtime zone availability and values
remain DUT questions.

## 10. Reset Examples

### Reset assert

```bash
./npu_se_test --json reset assert
```

Classification: `DESTRUCTIVE_REQUIRES_RECOVERY`.

Trace:

```text
CMD_ASSERT_RESET (0x88)
  -> handle_assert_reset()
  -> npu_reset_assert()
  -> select npu_fin_pll
  -> assert RISC/NOC/NPU reset and verify bit 0
  -> deassert NPU and NOC while RISC remains asserted
```

### Reset deassert

```bash
./npu_se_test --json reset deassert
```

Classification: `DESTRUCTIVE_REQUIRES_RECOVERY`.

Trace:

```text
CMD_DEASSERT_RESET (0x99)
  -> handle_deassert_reset()
  -> npu_reset_deassert()
  -> deassert RISC reset and verify bit 0
  -> select cmu_npu_xpll and wait for mux stabilization
```

**DESTRUCTIVE - DO NOT RUN UNTIL RECOVERY IS QUALIFIED.** Both commands can
interrupt NPU work and shared clock state. The current mailbox has no reset-state
query or numeric failure result. A failure can leave the request pending until
client timeout. Keep an independent console/power recovery path.

Potential later readback commands, only after the register transaction and state
semantics are qualified:

```bash
./npu_se_test --json reg read --addr 0x20840b40
./npu_se_test --json reg read --addr 0x20840b44
./npu_se_test --json reg read --addr 0x20840b48
./npu_se_test --json reg read --addr 0x230d0000
```

## Recommended First DUT Commands

Start with read-only commands and capture both stdout and stderr:

```bash
./npu_se_test --json status

./npu_se_test --json freq get

./npu_se_test --json thermal raw

./npu_se_test --json thermal get

./npu_se_test --json reg read --addr 0x20840b48
```

Then, only after those complete and the rate is approved, run the non-mutating
negative frequency request and verify that the rate remains observable:

```bash
./npu_se_test --json freq set --hz 0
./npu_se_test --json freq get
```

Do not put register writes or reset commands in the initial DUT sequence. Do not
run the invalid register or permission examples until the pending-error recovery
behavior and independent dmesg capture are qualified.

## SRS -> Command Mapping

The authoritative SRS text was not located in the inspected repository. The
feature assignment below follows the supplied F001-F007 descriptions and is
therefore a working mapping, not an SRS quotation.

| SRS | Example command | Concrete parameter | Expected driver behavior | Safe to run |
| --- | --- | --- | --- | --- |
| `NPU_F_001` register control | `./npu_se_test --json reg read --addr 0x20840b48` | `0x20840b48` RISC reset register | Read through `CMD_REG_CONTROL` and `npu_reg_ctl()` | `READ_ONLY_SAFE_CANDIDATE` |
| `NPU_F_002` frequency | `./npu_se_test --json freq set --hz 325000000` | A0 DT OPP candidate `325000000` Hz | CCF round/set/read path; actual readback requires separate `freq get` | `STATE_CHANGING_REQUIRES_RESTORE` |
| `NPU_F_003` reset | `./npu_se_test --json reset assert` | No parameter | `CMD_ASSERT_RESET` -> `npu_reset_assert()` | `DESTRUCTIVE_REQUIRES_RECOVERY` |
| `NPU_F_004` MPU/APU configuration | NO PHASE-1 COMMAND | None | Out of scope; no MPU/APU command is implemented | `BLOCKED_NO_SAFE_PARAMETER` |
| `NPU_F_005` thermal | `./npu_se_test --json thermal raw` | No parameter | Read 16 named thermal zones and publish mask/sentinels | `READ_ONLY_SAFE_CANDIDATE` |
| `NPU_F_006` PCIe/iATU | NO PHASE-1 COMMAND | None | Out of scope; no PCIe/iATU command is implemented | `BLOCKED_NO_SAFE_PARAMETER` |
| `NPU_F_007` real boot/init | NO PHASE-1 COMMAND | None | Driver initialization occurs from kernel init, not this CLI | `BLOCKED_NO_SAFE_PARAMETER` |

The exact VC acceptance criteria for these SRS IDs remain unconfirmed. In
particular, a command completion with `driver_result: null` is not proof of a
numeric driver failure or complete state restoration.

## Final Command Cheat Sheet

```bash
# Status - READ_ONLY_SAFE_CANDIDATE
./npu_se_test --json status

# Frequency - READ_ONLY_SAFE_CANDIDATE
./npu_se_test --json freq get

# Frequency - STATE_CHANGING_REQUIRES_RESTORE; A0 source candidate
./npu_se_test --json freq set --hz 325000000
./npu_se_test --json freq get

# Frequency negative - NEGATIVE_SAFE_CANDIDATE
./npu_se_test --json freq set --hz 0

# Thermal - READ_ONLY_SAFE_CANDIDATE
./npu_se_test --json thermal raw
./npu_se_test --json thermal get

# Register reads - READ_ONLY_SAFE_CANDIDATE, DUT qualification required
./npu_se_test --json reg read --addr 0x20840b40
./npu_se_test --json reg read --addr 0x20840b44
./npu_se_test --json reg read --addr 0x20840b48
./npu_se_test --json reg read --addr 0x230d0000

# Invalid address - NEGATIVE_SAFE_CANDIDATE; current ABI may show ETIMEDOUT
./npu_se_test --json reg read --addr 0x00000000

# Protected clock-mux write - NEGATIVE_SAFE_CANDIDATE; current ABI may show ETIMEDOUT
./npu_se_test --json reg write --addr 0x230d0000 --value 0x00000000

# Valid register write - BLOCKED_NO_SAFE_PARAMETER
# NO SOURCE-CONFIRMED SAFE REGISTER WRITE TARGET

# Reset - DESTRUCTIVE_REQUIRES_RECOVERY; do not run until qualified
./npu_se_test --json reset assert
./npu_se_test --json reset deassert
```
