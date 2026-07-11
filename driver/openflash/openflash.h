/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OPENFLASH_H_
#define _OPENFLASH_H_

#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "openflash_abi.h"

#define OPENFLASH_DRV_NAME "openflash"
#define OPENFLASH_MAX_QUEUES 64
#define OPENFLASH_DEFAULT_Q_DEPTH 128

struct openflash_queue {
	/* Serializes SQ descriptor writes and tail publication for this queue. */
	spinlock_t sq_lock;
	struct openflash_command *sq_cmds;
	dma_addr_t sq_dma;
	struct openflash_completion *cqes;
	dma_addr_t cq_dma;
	u16 qid;
	u16 depth;
	u16 sq_tail;
	u16 cq_head;
};

struct openflash_dev {
	struct pci_dev *pdev;
	void __iomem *bar;
	struct openflash_queue *queues;
	u16 nr_queues;
	u16 queue_depth;
};

int openflash_setup_admin_queue(struct openflash_dev *ofdev);
void openflash_teardown_admin_queue(struct openflash_dev *ofdev);

#endif /* _OPENFLASH_H_ */
