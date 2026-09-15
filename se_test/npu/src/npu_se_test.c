/* SPDX-License-Identifier: GPL-2.0-only */
#define _DEFAULT_SOURCE
#include "npu_transport.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Packet word offsets mirror drivers/misc/bos/npu_drv/egln1_npu_ctl.h. */
#define WORD_STATUS 0U
#define WORD_IMEM_WRITE_REQUEST 1U
#define WORD_IMEM_WRITE_ADDRESS 2U
#define WORD_IMEM_WRITE_VALUE 3U
#define WORD_IMEM_READ_REQUEST 4U
#define WORD_IMEM_READ_ADDRESS 5U
#define WORD_IMEM_READ_READY 6U
#define WORD_IMEM_READ_VALUE 7U
#define WORD_FREQ_TARGET 1U
#define WORD_FREQ_SET_DONE 2U
#define WORD_FREQ_ACTUAL 3U
#define WORD_FREQ_GET_DONE 4U
#define WORD_FREQ_RESULT 5U
#define WORD_RESET_DONE 1U
#define WORD_ATU_REGION 1U
#define WORD_ATU_DIRECTION 2U
#define WORD_ATU_TARGET_LO 3U
#define WORD_ATU_TARGET_HI 4U
#define WORD_ATU_DONE 5U
#define WORD_MPU_TYPE 1U
#define WORD_MPU_DONE 2U
#define WORD_THERMAL_SENSOR_COUNT 1U
#define WORD_THERMAL_VALID_MASK 2U
#define WORD_THERMAL_TEMP_BASE 3U
#define THERMAL_SENSOR_COUNT 16U

struct output {
	/* Select machine-readable output without changing mailbox behavior. */
	int json;
};

/* Print the accepted command grammar without opening hardware resources. */
static void usage(const char *program)
{
	fprintf(stderr,
		"Usage: %s [--json] [--bar4 <hex>] <command> ...\n"
		"Commands:\n"
		"  status\n"
		"  reg read --addr <hex>\n"
		"  reg write --addr <hex> --value <hex>\n"
		"  freq get | freq set --hz <value>\n"
		"  reset assert | reset deassert\n"
		"  thermal get | thermal raw\n"
		"BAR4 defaults to 0x%" PRIx64 "; override with --bar4 or NPU_BAR4_PHYS.\n",
		program, (uint64_t)NPU_DEFAULT_BAR4_PHYS);
}

static int parse_u64(const char *text, uint64_t *value)
{
	char *end;
	unsigned long long parsed;

	if (!text || !*text || text[0] == '-' || text[0] == '+')
		return -EINVAL;
	errno = 0;
	/* Base zero permits the hexadecimal values used by hardware interfaces. */
	parsed = strtoull(text, &end, 0);
	if (errno || *end != '\0')
		return -EINVAL;
	*value = parsed;
	return 0;
}

static int parse_u32(const char *text, uint32_t *value)
{
	uint64_t parsed;
	int ret = parse_u64(text, &parsed);

	if (ret || parsed > UINT_MAX)
		return -EINVAL;
	*value = (uint32_t)parsed;
	return 0;
}

static const char *errno_name(int result)
{
	if (result >= 0)
		return NULL;
	switch (-result) {
	case EBUSY: return "EBUSY";
	case EINVAL: return "EINVAL";
	case ENODATA: return "ENODATA";
	case EPERM: return "EPERM";
	case ETIMEDOUT: return "ETIMEDOUT";
	default: return strerror(-result);
	}
}

static int begin_command(struct npu_transport *transport)
{
	return npu_transport_begin(transport);
}

static void print_client_error(const struct output *output, const char *command,
			       int result)
{
	if (output->json)
		printf("{\"command\":\"%s\",\"result\":\"error\",\"driver_result\":null,\"error\":\"%s\"}\n",
		       command, errno_name(result));
}

/* Render the result only when this mailbox command actually returns errno. */
static int print_result(const struct output *output, const char *command,
			int driver_result, int driver_result_known)
{
	int success = !driver_result_known || driver_result == 0;

	if (output->json) {
		printf("{\"result\":\"%s\",\"command\":\"%s\",",
		       success ? "ok" : "error", command);
		if (driver_result_known)
			printf("\"driver_result\":%d", driver_result);
		else
			printf("\"driver_result\":null");
		if (!success)
			printf(",\"error\":\"%s\"", errno_name(driver_result));
		printf("}\n");
	} else if (driver_result_known) {
		printf("%s: driver_result=%d%s%s\n", command, driver_result,
		       driver_result ? " error=" : "",
		       driver_result ? errno_name(driver_result) : "");
	} else {
		printf("%s: completed (driver result is not exposed by this mailbox command)\n",
		       command);
	}
	return success ? 0 : driver_result;
}

static int require_option(int argc, char **argv, const char *name, uint32_t *value)
{
	int index;

	/* This validates syntax only; semantic values are deliberately driver-owned. */
	for (index = 0; index + 1 < argc; index++) {
		if (!strcmp(argv[index], name))
			return parse_u32(argv[index + 1], value);
	}
	return -EINVAL;
}

/* Report mailbox ownership/readiness without sending a driver command. */
static int command_status(struct npu_transport *transport, const struct output *output)
{
	uint32_t status = npu_transport_read(transport, WORD_STATUS);

	if (output->json)
		printf("{\"result\":\"ok\",\"command\":\"status\",\"mailbox_status\":%" PRIu32 ",\"ready\":%s,\"driver_result\":null}\n",
		       status, status == 0 ? "true" : "false");
	else
		printf("status: mailbox_status=0x%08" PRIx32 " ready=%s\n", status,
		       status == 0 ? "yes" : "no");
	return 0;
}

static int command_reg(struct npu_transport *transport, const struct output *output,
		       int argc, char **argv)
{
	uint32_t address, value;
	int ret;

	if (argc < 1 || (strcmp(argv[0], "read") && strcmp(argv[0], "write")))
		return -EINVAL;
	ret = require_option(argc - 1, argv + 1, "--addr", &address);
	if (ret)
		return ret;
	if (!strcmp(argv[0], "write")) {
		ret = require_option(argc - 1, argv + 1, "--value", &value);
		if (ret)
			return ret;
		ret = begin_command(transport);
		if (ret)
			return ret;
		/* Forward the unvalidated register request to npu_reg_ctl(). */
		npu_transport_write(transport, WORD_IMEM_WRITE_ADDRESS, address);
		npu_transport_write(transport, WORD_IMEM_WRITE_VALUE, value);
		npu_transport_write(transport, WORD_IMEM_WRITE_REQUEST, 1);
		npu_transport_write(transport, WORD_STATUS, NPU_CMD_REG_CONTROL);
		ret = npu_transport_wait_reg_write(transport);
		if (ret)
			return ret;
		if (output->json)
			printf("{\"result\":\"ok\",\"command\":\"reg_write\",\"address\":\"0x%08" PRIx32 "\",\"value\":\"0x%08" PRIx32 "\",\"driver_result\":null}\n", address, value);
		else
			printf("reg_write: address=0x%08" PRIx32 " value=0x%08" PRIx32 " completed (driver result unavailable)\n", address, value);
		return 0;
	}
	/* The read-ready flag, unlike status, is this command's completion signal. */
	ret = begin_command(transport);
	if (ret)
		return ret;
	npu_transport_write(transport, WORD_IMEM_READ_ADDRESS, address);
	npu_transport_write(transport, WORD_IMEM_READ_READY, 0);
	npu_transport_write(transport, WORD_IMEM_READ_REQUEST, 1);
	npu_transport_write(transport, WORD_STATUS, NPU_CMD_REG_CONTROL);
	ret = npu_transport_wait(transport, WORD_IMEM_READ_READY);
	if (ret)
		return ret;
	value = npu_transport_read(transport, WORD_IMEM_READ_VALUE);
	if (output->json)
		printf("{\"result\":\"ok\",\"command\":\"reg_read\",\"address\":\"0x%08" PRIx32 "\",\"value\":\"0x%08" PRIx32 "\",\"driver_result\":null}\n", address, value);
	else
		printf("reg_read: address=0x%08" PRIx32 " value=0x%08" PRIx32 " (driver result unavailable)\n", address, value);
	return 0;
}

static int command_freq(struct npu_transport *transport, const struct output *output,
			int argc, char **argv)
{
	uint32_t frequency;
	int ret;
	int32_t driver_result;

	if (argc != 1 && argc != 3)
		return -EINVAL;
	if (!strcmp(argv[0], "set")) {
		ret = require_option(argc - 1, argv + 1, "--hz", &frequency);
		if (ret)
			return ret;
		ret = begin_command(transport);
		if (ret)
			return ret;
		/* Frequency ABI includes a real result field, including driver errno. */
		npu_transport_write(transport, WORD_FREQ_TARGET, frequency);
		npu_transport_write(transport, WORD_FREQ_SET_DONE, 0);
		npu_transport_write(transport, WORD_FREQ_RESULT, (uint32_t)-EINPROGRESS);
		npu_transport_write(transport, WORD_STATUS, NPU_CMD_SET_FREQ);
		ret = npu_transport_wait(transport, WORD_FREQ_SET_DONE);
		if (ret)
			return ret;
		driver_result = (int32_t)npu_transport_read(transport, WORD_FREQ_RESULT);
		if (output->json)
			printf("{\"result\":\"%s\",\"command\":\"freq_set\",\"driver_result\":%d,\"requested_hz\":%" PRIu32 ",\"actual_hz\":null}\n", driver_result ? "error" : "ok", driver_result, frequency);
		else
			printf("freq_set: requested_hz=%" PRIu32 " driver_result=%d%s%s\n", frequency, driver_result, driver_result ? " error=" : "", driver_result ? errno_name(driver_result) : "");
		if (!driver_result)
			return 0;
		return driver_result;
	}
	if (strcmp(argv[0], "get"))
		return -EINVAL;
	/* The driver fills actual frequency and result before get-done. */
	ret = begin_command(transport);
	if (ret)
		return ret;
	npu_transport_write(transport, WORD_FREQ_ACTUAL, 0);
	npu_transport_write(transport, WORD_FREQ_GET_DONE, 0);
	npu_transport_write(transport, WORD_FREQ_RESULT, (uint32_t)-EINPROGRESS);
	npu_transport_write(transport, WORD_STATUS, NPU_CMD_GET_FREQ);
	ret = npu_transport_wait(transport, WORD_FREQ_GET_DONE);
	if (ret)
		return ret;
	driver_result = (int32_t)npu_transport_read(transport, WORD_FREQ_RESULT);
	frequency = npu_transport_read(transport, WORD_FREQ_ACTUAL);
	if (output->json)
		printf("{\"result\":\"%s\",\"command\":\"freq_get\",\"driver_result\":%d,\"actual_hz\":%" PRIu32 "}\n", driver_result ? "error" : "ok", driver_result, frequency);
	else
		printf("freq_get: actual_hz=%" PRIu32 " driver_result=%d%s%s\n", frequency, driver_result, driver_result ? " error=" : "", driver_result ? errno_name(driver_result) : "");
	return driver_result;
}

static int command_reset(struct npu_transport *transport, const struct output *output,
			 int argc, char **argv)
{
	uint32_t command;
	int ret;

	if (argc != 1)
		return -EINVAL;
	if (!strcmp(argv[0], "assert"))
		command = NPU_CMD_ASSERT_RESET;
	else if (!strcmp(argv[0], "deassert"))
		command = NPU_CMD_DEASSERT_RESET;
	else
		return -EINVAL;
	/* Reset completion has no result field in the current mailbox ABI. */
	ret = begin_command(transport);
	if (ret)
		return ret;
	npu_transport_write(transport, WORD_RESET_DONE, 0);
	npu_transport_write(transport, WORD_STATUS, command);
	ret = npu_transport_wait(transport, WORD_RESET_DONE);
	if (ret)
		return ret;
	return print_result(output, command == NPU_CMD_ASSERT_RESET ? "reset_assert" : "reset_deassert", 0, 0);
}

static int command_thermal(struct npu_transport *transport, const struct output *output,
			   int argc, char **argv)
{
	uint32_t valid_mask, sensor_count;
	int32_t temperatures[THERMAL_SENSOR_COUNT];
	int64_t total = 0;
	unsigned int index, count = 0;
	int ret;

	if (argc != 1 || (strcmp(argv[0], "get") && strcmp(argv[0], "raw")))
		return -EINVAL;
	ret = begin_command(transport);
	if (ret)
		return ret;
	/* Thermal publishes all samples before restoring READY. */
	npu_transport_write(transport, WORD_STATUS, NPU_CMD_GET_THERMAL);
	ret = npu_transport_wait_ready(transport);
	if (ret)
		return ret;
	sensor_count = npu_transport_read(transport, WORD_THERMAL_SENSOR_COUNT);
	valid_mask = npu_transport_read(transport, WORD_THERMAL_VALID_MASK);
	for (index = 0; index < THERMAL_SENSOR_COUNT; index++) {
		temperatures[index] = (int32_t)npu_transport_read(transport, WORD_THERMAL_TEMP_BASE + index);
		/* Exclude unavailable thermal zones from the calculated average. */
		if (valid_mask & (1U << index)) {
			total += temperatures[index];
			count++;
		}
	}
	if (!strcmp(argv[0], "get")) {
		if (!count)
			ret = -ENODATA;
		else
			ret = 0;
		if (output->json) {
			if (ret)
				printf("{\"result\":\"error\",\"command\":\"thermal_get\",\"driver_result\":null,\"sensor_count\":%" PRIu32 ",\"valid_count\":0,\"valid_mask\":\"0x%04" PRIx32 "\",\"error\":\"ENODATA\"}\n", sensor_count, valid_mask);
			else
				printf("{\"result\":\"ok\",\"command\":\"thermal_get\",\"driver_result\":null,\"sensor_count\":%" PRIu32 ",\"valid_count\":%u,\"valid_mask\":\"0x%04" PRIx32 "\",\"average_mdegc\":%" PRId64 ",\"average_source\":\"client\"}\n", sensor_count, count, valid_mask, total / (int64_t)count);
		}
		else
			printf("thermal_get: sensor_count=%" PRIu32 " valid_mask=0x%04" PRIx32 " average_mdegc=%" PRId64 "\n", sensor_count, valid_mask, count ? total / (int64_t)count : 0);
		return ret;
	}
	if (output->json) {
		printf("{\"result\":\"ok\",\"command\":\"thermal_raw\",\"driver_result\":null,\"sensor_count\":%" PRIu32 ",\"valid_mask\":\"0x%04" PRIx32 "\",\"temperatures_mdegc\":[", sensor_count, valid_mask);
		for (index = 0; index < THERMAL_SENSOR_COUNT; index++)
			printf("%s%d", index ? "," : "", temperatures[index]);
		printf("]}\n");
	} else {
		printf("thermal_raw: valid_mask=0x%04" PRIx32, valid_mask);
		for (index = 0; index < THERMAL_SENSOR_COUNT; index++)
			printf(" npu%u=%d", index, temperatures[index]);
		printf("\n");
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct npu_transport transport;
	struct output output = { 0 };
	uint64_t bar4_phys = NPU_DEFAULT_BAR4_PHYS;
	const char *environment_bar4;
	int environment_bar4_invalid = 0;
	int bar4_explicit = 0;
	int json_requested = 0;
	int command_index = 1;
	int ret;

	/* Environment supplies a board default; an explicit CLI option overrides it. */
	environment_bar4 = getenv("NPU_BAR4_PHYS");
	if (environment_bar4 && parse_u64(environment_bar4, &bar4_phys))
		environment_bar4_invalid = 1;
	/* Parse only global options before dispatching the subcommand's arguments. */
	while (command_index < argc && !strncmp(argv[command_index], "--", 2)) {
		if (!strcmp(argv[command_index], "--help")) {
			usage(argv[0]);
			return EXIT_SUCCESS;
		} else if (!strcmp(argv[command_index], "--json")) {
			output.json = 1;
			json_requested = 1;
			command_index++;
		} else if (!strcmp(argv[command_index], "--bar4") && command_index + 1 < argc) {
			ret = parse_u64(argv[command_index + 1], &bar4_phys);
			if (ret) {
				if (json_requested)
					printf("{\"command\":\"client\",\"result\":\"error\",\"driver_result\":null,\"error\":\"EINVAL\"}\n");
				else
					fprintf(stderr, "Invalid BAR4 address: %s\n", argv[command_index + 1]);
				return EXIT_FAILURE;
			}
			bar4_explicit = 1;
			command_index += 2;
		} else {
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (environment_bar4_invalid && !bar4_explicit) {
		if (json_requested)
			printf("{\"command\":\"client\",\"result\":\"error\",\"driver_result\":null,\"error\":\"EINVAL\"}\n");
		else
			fprintf(stderr, "Invalid NPU_BAR4_PHYS: %s\n", environment_bar4);
		return EXIT_FAILURE;
	}
	if (command_index >= argc) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	if (!strcmp(argv[command_index], "mpu") || !strcmp(argv[command_index], "pcie")) {
		if (output.json)
			printf("{\"command\":\"%s\",\"result\":\"error\",\"driver_result\":null,\"error\":\"ENOTSUP\"}\n", argv[command_index]);
		else
			fprintf(stderr, "unsupported command in Phase 1: %s\n", argv[command_index]);
		return EXIT_FAILURE;
	}
	/* Every command except usage operates through the existing BAR4 mailbox. */
	ret = npu_transport_open(&transport, bar4_phys);
	if (ret) {
		if (output.json)
			printf("{\"command\":\"%s\",\"result\":\"error\",\"driver_result\":null,\"error\":\"%s\"}\n",
			       argv[command_index], errno_name(ret));
		else
			fprintf(stderr, "Cannot map BAR4 at 0x%" PRIx64 ": %s (%d)\n",
				bar4_phys, errno_name(ret), ret);
		return EXIT_FAILURE;
	}
	/* Dispatch strictly to mailbox-backed handlers; there is no driver reimplementation. */
	if (!strcmp(argv[command_index], "status"))
		ret = command_status(&transport, &output);
	else if (!strcmp(argv[command_index], "reg"))
		ret = command_reg(&transport, &output, argc - command_index - 1, argv + command_index + 1);
	else if (!strcmp(argv[command_index], "freq"))
		ret = command_freq(&transport, &output, argc - command_index - 1, argv + command_index + 1);
	else if (!strcmp(argv[command_index], "reset"))
		ret = command_reset(&transport, &output, argc - command_index - 1, argv + command_index + 1);
	else if (!strcmp(argv[command_index], "thermal"))
		ret = command_thermal(&transport, &output, argc - command_index - 1, argv + command_index + 1);
	else
		ret = -EINVAL;
	if (ret) {
		if (output.json)
			print_client_error(&output, argv[command_index], ret);
		else {
			fprintf(stderr, "command failed: %s (%d)\n", errno_name(ret), ret);
			usage(argv[0]);
		}
	}
	npu_transport_close(&transport);
	return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}
