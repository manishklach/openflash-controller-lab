/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _OPENFLASH_H_
#define _OPENFLASH_H_

#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define OPENFLASH_DRV_NAME "openflash"
#define OPENFLASH_ABI_VERSION 1
#define OPENFLASH_MAX_QUEUES 64
#define OPENFLASH_DEFAULT_Q_DEPTH 128

struct openflash_queue {
	spinlock_t sq_lock;
	void *sq_cmds;
	dma_addr_t sq_dma;
	void *cqes;
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

#endif /* _OPENFLASH_H_ */

