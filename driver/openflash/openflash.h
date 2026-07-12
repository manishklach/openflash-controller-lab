/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OPENFLASH_H_
#define _OPENFLASH_H_

#include <linux/pci.h>
#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "openflash_abi.h"

#define OPENFLASH_DRV_NAME "openflash"
#define OPENFLASH_MAX_QUEUES 64
#define OPENFLASH_DEFAULT_Q_DEPTH 128
#define OPENFLASH_BLOCK_SIZE 4096
#define OPENFLASH_SECTORS_PER_BLOCK (OPENFLASH_BLOCK_SIZE >> SECTOR_SHIFT)

struct openflash_request {
	struct request *rq;
	struct openflash_sgl_desc *sgl;
	dma_addr_t sgl_dma;
	u16 nr_mapped;
	enum dma_data_direction dma_dir;
};

struct openflash_queue {
	/* Serializes SQ descriptor writes and tail publication for this queue. */
	spinlock_t sq_lock;
	/* Admin commands may sleep while waiting for their phase-tagged completion. */
	struct mutex admin_lock;
	struct openflash_command *sq_cmds;
	dma_addr_t sq_dma;
	struct openflash_completion *cqes;
	dma_addr_t cq_dma;
	u16 qid;
	u16 depth;
	u16 sq_tail;
	u16 cq_head;
	u8 cq_phase;
	u16 next_cid;
	struct openflash_request *requests;
	struct completion admin_done;
	u16 admin_cid;
	u16 admin_status;
	u64 admin_result;
	int admin_error;
	bool admin_pending;
};

struct openflash_dev {
	struct pci_dev *pdev;
	void __iomem *bar;
	struct openflash_queue *queues;
	u16 nr_queues;
	u16 queue_depth;
	u64 capacity_blocks;
	struct blk_mq_tag_set tag_set;
	struct gendisk *disk;
	struct work_struct reset_work;
	atomic_t reset_pending;
};

int openflash_setup_admin_queue(struct openflash_dev *ofdev);
int openflash_admin_identify(struct openflash_dev *ofdev);
int openflash_setup_io_queue(struct openflash_dev *ofdev);
int openflash_reset_controller(struct openflash_dev *ofdev);
void openflash_teardown_io_queue(struct openflash_dev *ofdev);
void openflash_fail_io_requests(struct openflash_dev *ofdev, blk_status_t status);
int openflash_register_block_device(struct openflash_dev *ofdev);
void openflash_unregister_block_device(struct openflash_dev *ofdev);
void openflash_teardown_admin_queue(struct openflash_dev *ofdev);

#endif /* _OPENFLASH_H_ */
