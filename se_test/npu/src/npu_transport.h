/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_TRANSPORT_H
#define NPU_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

/* Local endpoint IMEM address used by the host driver mailbox mapping. */
#define NPU_DEFAULT_BAR4_PHYS 0x30000000ULL
#define NPU_MAILBOX_WORDS 169U

/* Command values consumed by the NPU host driver's mailbox workqueue. */
enum npu_mailbox_command {
	NPU_CMD_SET_FREQ = 0x44U,
	NPU_CMD_GET_FREQ = 0x45U,
	NPU_CMD_ASSERT_RESET = 0x88U,
	NPU_CMD_DEASSERT_RESET = 0x99U,
	NPU_CMD_REG_CONTROL = 0xaaU,
	NPU_CMD_GET_THERMAL = 0xccU,
	NPU_CMD_ATU_PROG = 0xddU,
	NPU_CMD_INIT_MPU = 0xeeU,
};

/* Values forwarded unchanged to npu_atu_set() as the iATU direction. */
enum npu_atu_direction {
	NPU_ATU_OUTBOUND = 0U,
	NPU_ATU_INBOUND = 1U,
};

/* State required to map the BAR4 mailbox page through /dev/mem. */
struct npu_transport {
	int fd;
	void *map;
	uint64_t bar4_phys;
	uint64_t page_base;
	unsigned char *packet;
	size_t map_size;
};

/* Open /dev/mem and map enough BAR4 space for the largest mailbox response. */
int npu_transport_open(struct npu_transport *transport, uint64_t bar4_phys);
/* Unmap the mailbox and close /dev/mem. */
void npu_transport_close(struct npu_transport *transport);
/* Read one 32-bit mailbox word at the supplied word offset. */
uint32_t npu_transport_read(const struct npu_transport *transport, unsigned int word);
/* Write one 32-bit mailbox word and publish it before the command is issued. */
void npu_transport_write(const struct npu_transport *transport, unsigned int word,
			 uint32_t value);
/* Verify READY, clear the previous union payload, and reserve the mailbox. */
int npu_transport_begin(struct npu_transport *transport);
/* Wait for READY and a command-specific completion flag. */
int npu_transport_wait(const struct npu_transport *transport, unsigned int done_word);
/* Wait for a register write's request flag to be cleared by the driver. */
int npu_transport_wait_reg_write(const struct npu_transport *transport);
/* Wait only for READY, for commands whose ABI has no completion word. */
int npu_transport_wait_ready(const struct npu_transport *transport);

#endif
