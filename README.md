# npu-drivers
NPU-Driver-App

# NPU SE Test Utility

`npu_se_test` sends commands through the existing NPU host mailbox over BAR4. It maps `/dev/mem`, writes the same packet layout used by `tools/testing/bos/eagle_n1_npu`, and waits for the real NPU driver workqueue. It does not implement driver behavior in userspace.

## Build

```bash
cd tools/testing/bos/se_test/npu
make
```

The default compiler is `aarch64-linux-gnu-gcc`, matching the existing NPU test app. Override `CC` for a native diagnostic build:

```bash
make clean && make CC=gcc
```

The binary is `build/npu_se_test`.

## Run

The default BAR4 physical address is `0x44000000`, matching the existing NPU MMIO tests. For a different mapping, pass `--bar4` or set `NPU_BAR4_PHYS`. `/dev/mem` normally requires root privileges.

```bash
sudo ./build/npu_se_test status
sudo ./build/npu_se_test --bar4 0x44000000 freq get
sudo NPU_BAR4_PHYS=0x44000000 ./build/npu_se_test --json freq set --hz 800000000
```

## Commands

| CLI command | Mailbox command | Driver function | Parameters |
| --- | --- | --- | --- |
| `status` | None; reads mailbox status | None | None |
| `reg read --addr <hex>` | `CMD_REG_CONTROL` | `npu_reg_ctl()` | Any 32-bit address |
| `reg write --addr <hex> --value <hex>` | `CMD_REG_CONTROL` | `npu_reg_ctl()` | Any 32-bit address and value |
| `freq get` | `CMD_GET_NPU_FREQ` | `npu_clock_get()` | None |
| `freq set --hz <value>` | `CMD_SET_NPU_FREQ` | `npu_clock_set()` | Any 32-bit frequency, including `0` |
| `reset assert` | `CMD_ASSERT_RESET` | `npu_reset_assert()` | None |
| `reset deassert` | `CMD_DEASSERT_RESET` | `npu_reset_deassert()` | None |
| `mpu set --id <id>` | `CMD_INIT_MPU` | `npu_mpu_init()` | Any 32-bit MPU type/configuration ID |
| `thermal get` | `CMD_GET_THERMAL` | NPU thermal-zone handler | None |
| `thermal raw` | `CMD_GET_THERMAL` | NPU thermal-zone handler | None |
| `pcie outbound --region <id> --target <hex>` | `CMD_ATU_PROG` | `npu_atu_set()` | Region, target address |
| `pcie inbound --bar <id> --target <hex>` | `CMD_ATU_PROG` | `npu_atu_set()` | The `--bar` value is forwarded as the driver’s `region_sel`; target address |
| `pcie config --direction <value> --region <id> --target <hex>` | `CMD_ATU_PROG` | `npu_atu_set()` | Raw direction, region, target address |

Examples, including values intentionally expected to be rejected by the driver:

```bash
sudo ./build/npu_se_test reg read --addr 0x12340000
sudo ./build/npu_se_test reg read --addr 0xDEADBEEF
sudo ./build/npu_se_test freq set --hz 750000000
sudo ./build/npu_se_test freq set --hz 0
sudo ./build/npu_se_test mpu set --id 1
sudo ./build/npu_se_test mpu set --id 255
sudo ./build/npu_se_test pcie inbound --bar 0 --target 0x80000000
sudo ./build/npu_se_test pcie inbound --bar 0 --target 0x80000001
```

## Result Semantics and Limits

`freq get` and `freq set` return the actual `result` field supplied by the driver. `freq get` also returns the driver’s actual read-back frequency. `thermal get` returns the average of valid sensor temperatures in millidegrees Celsius; `thermal raw` reports all 16 raw sensor values and their valid mask.

The current mailbox ABI has no result field for register, reset, MPU, ATU, or thermal commands. Their driver error paths leave the mailbox command pending rather than publishing an errno, so this tool reports a completion timeout when that occurs. It does not manufacture an errno. Consult the kernel log for the underlying driver error.

There is no reset-state query, MPU/APU dump, or PCIe/ATU readback command in the current mailbox interface. The driver terminology is **MPU** (`CMD_INIT_MPU` and `npu_mpu_init()`), not APU/MAPU/SAPU. The tool intentionally does not add a boot/init command; driver initialization remains the module initialization path.
