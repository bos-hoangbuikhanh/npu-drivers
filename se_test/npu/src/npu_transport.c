/* SPDX-License-Identifier: GPL-2.0-only */
#define _DEFAULT_SOURCE

#include "npu_transport.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* Common packet status and polling values from the host mailbox ABI. */
#define NPU_STATUS_WORD 0U
#define NPU_STATUS_READY 0U
#define NPU_REQ_DONE 1U
#define NPU_POLL_INTERVAL_US 1000U
#define NPU_TIMEOUT_MS 2000U

static int wait_until_ready(const struct npu_transport *transport,
			    unsigned int done_word, int require_done)
{
	struct timespec deadline;
	uint32_t status;
	uint32_t done;

	if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
		return -errno;
	deadline.tv_sec += NPU_TIMEOUT_MS / 1000U;
	deadline.tv_nsec += (NPU_TIMEOUT_MS % 1000U) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000L;
	}

	for (;;) {
		status = npu_transport_read(transport, NPU_STATUS_WORD);
		done = npu_transport_read(transport, done_word);
		if (status == NPU_STATUS_READY &&
		    (!require_done || done == NPU_REQ_DONE)) {
			__sync_synchronize();
			return 0;
		}
		struct timespec now;
		if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
			return -errno;
		if (now.tv_sec > deadline.tv_sec ||
		    (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))
			return -ETIMEDOUT;
		usleep(NPU_POLL_INTERVAL_US);
	}
}

int npu_transport_open(struct npu_transport *transport, uint64_t bar4_phys)
{
	long page_size;
	uint64_t page_mask;
	size_t page_offset;

	if (!transport)
		return -EINVAL;

	*transport = (struct npu_transport){ .fd = -1 };
	page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0)
		return -errno;
	/* /dev/mem mappings must begin at a page boundary. */
	page_mask = ~((uint64_t)page_size - 1U);
	transport->bar4_phys = bar4_phys;
	transport->page_base = bar4_phys & page_mask;
	page_offset = (size_t)(bar4_phys - transport->page_base);
	/* The packet includes the 672-byte build-info response plus status. */
	transport->map_size = page_offset + 1024U;
	transport->fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (transport->fd < 0)
		return -errno;
	transport->map = mmap(NULL, transport->map_size, PROT_READ | PROT_WRITE,
				     MAP_SHARED, transport->fd,
				     (off_t)transport->page_base);
	if (transport->map == MAP_FAILED) {
		int error = -errno;
		close(transport->fd);
		transport->fd = -1;
		transport->map = NULL;
		return error;
	}
	transport->packet = (unsigned char *)transport->map + page_offset;
	return 0;
}

void npu_transport_close(struct npu_transport *transport)
{
	if (!transport)
		return;
	if (transport->map)
		munmap(transport->map, transport->map_size);
	if (transport->fd >= 0)
		close(transport->fd);
	*transport = (struct npu_transport){ .fd = -1 };
}

uint32_t npu_transport_read(const struct npu_transport *transport, unsigned int word)
{
	/* volatile prevents cached accesses to memory the driver updates asynchronously. */
	volatile uint32_t *registers = (volatile uint32_t *)transport->packet;

	return registers[word];
}

void npu_transport_write(const struct npu_transport *transport, unsigned int word,
			 uint32_t value)
{
	volatile uint32_t *registers = (volatile uint32_t *)transport->packet;

	registers[word] = value;
	/* Publish packet fields before a later status write triggers the driver. */
	__sync_synchronize();
}

int npu_transport_begin(struct npu_transport *transport)
{
	unsigned int word;

	if (!transport)
		return -EINVAL;
	if (npu_transport_read(transport, NPU_STATUS_WORD) != NPU_STATUS_READY)
		return -EBUSY;
	for (word = 1; word < NPU_MAILBOX_WORDS; word++)
		npu_transport_write(transport, word, 0);
	return 0;
}

int npu_transport_wait(const struct npu_transport *transport, unsigned int done_word)
{
	return wait_until_ready(transport, done_word, 1);
}

int npu_transport_wait_reg_write(const struct npu_transport *transport)
{
	struct timespec deadline;

	if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
		return -errno;
	deadline.tv_sec += NPU_TIMEOUT_MS / 1000U;
	deadline.tv_nsec += (NPU_TIMEOUT_MS % 1000U) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000L;
	}
	for (;;) {
		struct timespec now;
		if (npu_transport_read(transport, NPU_STATUS_WORD) == NPU_STATUS_READY &&
		    npu_transport_read(transport, 1U) == 0U) {
			__sync_synchronize();
			return 0;
		}
		if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
			return -errno;
		if (now.tv_sec > deadline.tv_sec ||
		    (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))
			return -ETIMEDOUT;
		usleep(NPU_POLL_INTERVAL_US);
	}
}

int npu_transport_wait_ready(const struct npu_transport *transport)
{
	return wait_until_ready(transport, 0U, 0);
}
