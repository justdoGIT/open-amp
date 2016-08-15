/*
 * Copyright (c) 2014, Mentor Graphics Corporation
 * All rights reserved.
 * Copyright (c) 2015 Xilinx, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of Mentor Graphics Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**************************************************************************
 * FILE NAME
 *
 *       zynqmp_remoteproc_r5.c
 *
 * DESCRIPTION
 *
 *       This file is the Implementation of IPC hardware layer interface
 *       for Xilinx Zynq UltraScale+ MPSoC system.
 *
 **************************************************************************/

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "metal/io.h"
#include "metal/device.h"
#include "metal/utilities.h"
#include "metal/atomic.h"
#include "metal/irq.h"
#include "metal/cpu.h"
#include "metal/alloc.h"
#include "openamp/hil.h"
#include "openamp/remoteproc_plat.h"
#include "openamp/virtqueue.h"

/* IPI REGs OFFSET */
#define IPI_TRIG_OFFSET  0x00000000 /** IPI trigger register offset */
#define IPI_OBS_OFFSET   0x00000004 /** IPI observation register offset */
#define IPI_ISR_OFFSET   0x00000010 /* IPI interrupt status register offset */
#define IPI_IMR_OFFSET   0x00000014 /* IPI interrupt mask register offset */
#define IPI_IER_OFFSET   0x00000018 /* IPI interrupt enable register offset */
#define IPI_IDR_OFFSET   0x0000001C /* IPI interrupt disable register offset */

#define _rproc_wait() metal_cpu_yield()

#define DEBUG 1

/* -- FIX ME: ipi info is to be defined -- */
struct ipi_info {
	uint32_t ipi_chn_mask;
	int need_reg;
	atomic_int sync;
};

/*--------------------------- Declare Functions ------------------------ */
static int _enable_interrupt(struct proc_vring *vring_hw);
static void _notify(int cpu_id, struct proc_intr *intr_info);
static int _boot_cpu(int cpu_id, unsigned int load_addr);
static void _shutdown_cpu(int cpu_id);
static int _poll(struct hil_proc *proc, int nonblock);
static struct hil_proc *_initialize(void *pdata, int cpu_id);
static void _release(struct hil_proc *proc);

/*--------------------------- Globals ---------------------------------- */
struct hil_platform_ops zynqmp_a53_r5_proc_ops = {
	.enable_interrupt     = _enable_interrupt,
	.notify               = _notify,
	.boot_cpu             = _boot_cpu,
	.shutdown_cpu         = _shutdown_cpu,
	.poll                 = _poll,
	.initialize    = _initialize,
	.release    = _release,
};

static int _enable_interrupt(struct proc_vring *vring_hw)
{
	(void)vring_hw;
	return 0;
}

static void _notify(int cpu_id, struct proc_intr *intr_info)
{

	(void)cpu_id;
	struct ipi_info *ipi = (struct ipi_info *)(intr_info->data);
	if (ipi == NULL)
		return;

	/* Trigger IPI */
	metal_io_write32(intr_info->io, IPI_TRIG_OFFSET, ipi->ipi_chn_mask);
}

static int _boot_cpu(int cpu_id, unsigned int load_addr)
{
	(void)cpu_id;
	(void)load_addr;
	return -1;
}

static void _shutdown_cpu(int cpu_id)
{
	(void)cpu_id;
	return;
}

static int _poll(struct hil_proc *proc, int nonblock)
{
	struct proc_vring *vring;
	struct ipi_info *ipi;
	struct metal_io_region *io;

	vring = &proc->vdev.vring_info[0];
	ipi = (struct ipi_info *)(vring->intr_info.data);
	io = vring->intr_info.io;
	while(1) {
		unsigned int ipi_intr_status =
		    (unsigned int)metal_io_read32(io, IPI_ISR_OFFSET);
		if (ipi_intr_status & ipi->ipi_chn_mask) {
			metal_io_write32(io, IPI_ISR_OFFSET,
					ipi->ipi_chn_mask);
			virtqueue_notification(vring->vq);
			return 0;
		} else if (nonblock) {
			return -EAGAIN;
		}
		_rproc_wait();
	}
}

static struct hil_proc * _initialize(void *pdata, int cpu_id)
{
	(void) cpu_id;

	struct hil_proc *proc;
	int ret;
	struct metal_io_region *io;
	struct proc_intr *intr_info;
	struct ipi_info *ipi;
	unsigned int ipi_intr_status;

	/* Allocate memory for proc instance */
	proc = metal_allocate_memory(sizeof(struct hil_proc));
	if (!proc) {
		openamp_dbg("failed to allocate hil_proc mem\n");
		return NULL;
	}
	memset(proc, 0, sizeof(struct hil_proc));

	ret = rproc_init_plat_data(pdata, proc);
	if (ret) {
		openamp_dbg("failed to allocate hil_proc mem\n");
		goto error;
	}
	intr_info = &(proc->vdev.vring_info[0].intr_info);
	io = intr_info->io;
	ipi = (struct ipi_info *)(intr_info->data);
	ipi_intr_status =
	    (unsigned int)metal_io_read32(io, IPI_ISR_OFFSET);
	if (ipi_intr_status & ipi->ipi_chn_mask) {
		metal_io_write32(io, IPI_ISR_OFFSET, ipi->ipi_chn_mask);
	}
	metal_io_write32(io, IPI_IDR_OFFSET, ipi->ipi_chn_mask);
	atomic_store(&ipi->sync, 1);
	return proc;
error:
	if (proc) {
		rproc_close_plat(proc);
		metal_free_memory(proc);
	}
	return NULL;
}

static void _release(struct hil_proc *proc)
{
	if (proc) {
		rproc_close_plat(proc);
		metal_free_memory(proc);
	}
}

