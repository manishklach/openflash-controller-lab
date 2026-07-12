// SPDX-License-Identifier: GPL-2.0-only
#include <linux/dma-mapping.h>
#include <linux/bio.h>
#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>

#include "openflash.h"

/* Replace with an allocated experimental PCI ID before hardware/emulator binding. */
#define OPENFLASH_VENDOR_ID 0x1d1d
#define OPENFLASH_DEVICE_ID 0xf15a

static void openflash_reset_work(struct work_struct *work)
{
	struct openflash_dev *ofdev = container_of(work, struct openflash_dev,
					     reset_work);
	int ret;

	if (!ofdev->disk)
		goto done;
	blk_mq_freeze_queue(ofdev->disk->queue);
	/* A timed-out command may have reached media, so never replay it. */
	openflash_fail_io_requests(ofdev, BLK_STS_TIMEOUT);
	ret = openflash_reset_controller(ofdev);
	if (ret)
		dev_err(&ofdev->pdev->dev, "controller recovery failed: %d\n", ret);
	blk_mq_unfreeze_queue(ofdev->disk->queue);
done:
	atomic_set(&ofdev->reset_pending, 0);
}

static enum blk_eh_timer_return openflash_timeout(struct request *rq)
{
	struct openflash_dev *ofdev = rq->q->queuedata;

	if (atomic_cmpxchg(&ofdev->reset_pending, 0, 1) == 0)
		schedule_work(&ofdev->reset_work);
	return BLK_EH_DONE;
}

static blk_status_t openflash_queue_rq(struct blk_mq_hw_ctx *hctx,
					const struct blk_mq_queue_data *bd)
{
	struct openflash_dev *ofdev = hctx->queue->queuedata;
	struct openflash_queue *queue = &ofdev->queues[1];
	struct openflash_request *request;
	struct openflash_command *cmd;
	struct request *rq = bd->rq;
	struct bio_vec bvec;
	struct req_iterator iter;
	enum dma_data_direction dma_dir;
	dma_addr_t dma = 0;
	u64 lba = blk_rq_pos(rq) / OPENFLASH_SECTORS_PER_BLOCK;
	unsigned int bytes = blk_rq_bytes(rq);
	u32 nblocks = bytes / OPENFLASH_BLOCK_SIZE;
	unsigned long flags;
	u16 cid = rq->tag + 1;
	u16 sq_slot;
	u8 opcode;
	u16 mapped = 0;

	if (blk_rq_pos(rq) % OPENFLASH_SECTORS_PER_BLOCK ||
	    bytes % OPENFLASH_BLOCK_SIZE || cid >= queue->depth)
		return BLK_STS_IOERR;

	request = &queue->requests[rq->tag];

	switch (req_op(rq)) {
	case REQ_OP_READ:
		opcode = OPENFLASH_OP_READ;
		dma_dir = DMA_FROM_DEVICE;
		break;
	case REQ_OP_WRITE:
		opcode = OPENFLASH_OP_WRITE;
		dma_dir = DMA_TO_DEVICE;
		break;
	case REQ_OP_FLUSH:
		opcode = OPENFLASH_OP_FLUSH;
		goto submit;
	case REQ_OP_DISCARD:
		opcode = OPENFLASH_OP_DISCARD;
		goto submit;
	default:
		return BLK_STS_NOTSUPP;
	}

	if (!bytes || blk_rq_nr_phys_segments(rq) > OPENFLASH_MAX_SGL_ENTRIES)
		return BLK_STS_IOERR;
	rq_for_each_segment(bvec, rq, iter) {
		dma = dma_map_bvec(&ofdev->pdev->dev, &bvec, dma_dir, 0);
		if (dma_mapping_error(&ofdev->pdev->dev, dma))
			goto unmap;
		request->sgl[mapped].addr = cpu_to_le64(dma);
		request->sgl[mapped].length = cpu_to_le32(bvec.bv_len);
		request->sgl[mapped].reserved = 0;
		mapped++;
	}

submit:
	spin_lock_irqsave(&queue->sq_lock, flags);
	sq_slot = queue->sq_tail;
	if (unlikely(request->rq)) {
		spin_unlock_irqrestore(&queue->sq_lock, flags);
		while (mapped) {
			mapped--;
			dma_unmap_page(&ofdev->pdev->dev,
				le64_to_cpu(request->sgl[mapped].addr),
				le32_to_cpu(request->sgl[mapped].length), dma_dir);
		}
		return BLK_STS_RESOURCE;
	}
	cmd = &queue->sq_cmds[sq_slot];
	memset(cmd, 0, sizeof(*cmd));
	cmd->opcode = opcode;
	cmd->qid = cpu_to_le16(queue->qid);
	cmd->cid = cpu_to_le16(cid);
	cmd->lba = cpu_to_le64(lba);
	cmd->nblocks = cpu_to_le32(nblocks);
	if (mapped) {
		request->nr_mapped = mapped;
		request->dma_dir = dma_dir;
		cmd->flags = OPENFLASH_CMD_F_SGL;
		cmd->control = cpu_to_le32(mapped);
		cmd->data_addr = cpu_to_le64(request->sgl_dma);
	} else {
		request->nr_mapped = 0;
	}
	request->rq = rq;
	blk_mq_start_request(rq);
	dma_wmb();
	queue->sq_tail = (queue->sq_tail + 1) % queue->depth;
	writel(queue->sq_tail, ofdev->bar + OPENFLASH_SQ_TAIL_DB(queue->qid));
	spin_unlock_irqrestore(&queue->sq_lock, flags);
	return BLK_STS_OK;

unmap:
	while (mapped) {
		mapped--;
		dma_unmap_page(&ofdev->pdev->dev,
			le64_to_cpu(request->sgl[mapped].addr),
			le32_to_cpu(request->sgl[mapped].length), dma_dir);
	}
	return BLK_STS_RESOURCE;
}

static const struct blk_mq_ops openflash_mq_ops = {
	.queue_rq = openflash_queue_rq,
	.timeout = openflash_timeout,
};

static const struct block_device_operations openflash_fops = {
	.owner = THIS_MODULE,
};

int openflash_register_block_device(struct openflash_dev *ofdev)
{
	struct request_queue *queue;
	int ret;

	ofdev->tag_set.ops = &openflash_mq_ops;
	ofdev->tag_set.nr_hw_queues = 1;
	ofdev->tag_set.queue_depth = ofdev->queue_depth - 1;
	ofdev->tag_set.numa_node = NUMA_NO_NODE;
	ofdev->tag_set.cmd_size = 0;
	ofdev->tag_set.driver_data = ofdev;
	ret = blk_mq_alloc_tag_set(&ofdev->tag_set);
	if (ret)
		return ret;
	ofdev->disk = blk_mq_alloc_disk(&ofdev->tag_set, ofdev);
	if (IS_ERR(ofdev->disk)) {
		ret = PTR_ERR(ofdev->disk);
		ofdev->disk = NULL;
		goto free_tag_set;
	}
	queue = ofdev->disk->queue;
	queue->queuedata = ofdev;
	blk_queue_logical_block_size(queue, OPENFLASH_BLOCK_SIZE);
	blk_queue_physical_block_size(queue, OPENFLASH_BLOCK_SIZE);
	blk_queue_max_hw_sectors(queue, (ofdev->queue_depth - 1) *
				 OPENFLASH_SECTORS_PER_BLOCK);
	blk_queue_max_segments(queue, OPENFLASH_MAX_SGL_ENTRIES);
	ofdev->disk->major = 0;
	ofdev->disk->first_minor = 0;
	ofdev->disk->minors = 1;
	ofdev->disk->fops = &openflash_fops;
	ofdev->disk->private_data = ofdev;
	snprintf(ofdev->disk->disk_name, DISK_NAME_LEN, "openflash0");
	set_capacity(ofdev->disk, ofdev->capacity_blocks * OPENFLASH_SECTORS_PER_BLOCK);
	ret = device_add_disk(&ofdev->pdev->dev, ofdev->disk, NULL);
	if (ret)
		goto put_disk;
	return 0;

put_disk:
	put_disk(ofdev->disk);
	ofdev->disk = NULL;
free_tag_set:
	blk_mq_free_tag_set(&ofdev->tag_set);
	return ret;
}

void openflash_unregister_block_device(struct openflash_dev *ofdev)
{
	if (!ofdev->disk)
		return;
	del_gendisk(ofdev->disk);
	openflash_fail_io_requests(ofdev, BLK_STS_IOERR);
	put_disk(ofdev->disk);
	ofdev->disk = NULL;
	blk_mq_free_tag_set(&ofdev->tag_set);
}

static int openflash_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct openflash_dev *ofdev;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "64-bit DMA is required\n");

	ret = pcim_iomap_regions(pdev, BIT(0), OPENFLASH_DRV_NAME);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to map BAR0\n");

	ofdev = devm_kzalloc(&pdev->dev, sizeof(*ofdev), GFP_KERNEL);
	if (!ofdev)
		return -ENOMEM;

	ofdev->pdev = pdev;
	ofdev->bar = pcim_iomap_table(pdev)[0];
	ofdev->queue_depth = OPENFLASH_DEFAULT_Q_DEPTH;
	INIT_WORK(&ofdev->reset_work, openflash_reset_work);
	atomic_set(&ofdev->reset_pending, 0);
	if (readl(ofdev->bar + OPENFLASH_REG_ABI_VERSION) != OPENFLASH_ABI_VERSION)
		return dev_err_probe(&pdev->dev, -EPROTO, "unsupported controller ABI\n");
	pci_set_master(pdev);
	pci_set_drvdata(pdev, ofdev);

	ret = openflash_setup_admin_queue(ofdev);
	if (ret) {
		pci_clear_master(pdev);
		return dev_err_probe(&pdev->dev, ret, "failed to start admin queue\n");
	}
	ret = openflash_admin_identify(ofdev);
	if (ret) {
		openflash_teardown_admin_queue(ofdev);
		pci_clear_master(pdev);
		return dev_err_probe(&pdev->dev, ret, "identify command failed\n");
	}
	ret = openflash_setup_io_queue(ofdev);
	if (ret) {
		openflash_teardown_admin_queue(ofdev);
		pci_clear_master(pdev);
		return dev_err_probe(&pdev->dev, ret, "failed to create I/O queue\n");
	}
	ret = openflash_register_block_device(ofdev);
	if (ret) {
		openflash_teardown_io_queue(ofdev);
		openflash_teardown_admin_queue(ofdev);
		pci_clear_master(pdev);
		return dev_err_probe(&pdev->dev, ret, "failed to register block disk\n");
	}

	dev_info(&pdev->dev,
		 "ABI 0x%08x I/O queue ready, capacity %llu blocks\n",
		 OPENFLASH_ABI_VERSION, ofdev->capacity_blocks);
	return 0;
}

static void openflash_remove(struct pci_dev *pdev)
{
	struct openflash_dev *ofdev = pci_get_drvdata(pdev);

	/* Future implementation must quiesce and drain queues before resources unwind. */
	if (ofdev) {
		cancel_work_sync(&ofdev->reset_work);
		openflash_unregister_block_device(ofdev);
		openflash_teardown_io_queue(ofdev);
		openflash_teardown_admin_queue(ofdev);
		pci_clear_master(pdev);
	}
}

static const struct pci_device_id openflash_id_table[] = {
	{ PCI_DEVICE(OPENFLASH_VENDOR_ID, OPENFLASH_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, openflash_id_table);

static struct pci_driver openflash_driver = {
	.name = OPENFLASH_DRV_NAME,
	.id_table = openflash_id_table,
	.probe = openflash_probe,
	.remove = openflash_remove,
};
module_pci_driver(openflash_driver);

MODULE_AUTHOR("OpenFlash contributors");
MODULE_DESCRIPTION("OpenFlash experimental managed NAND controller scaffold");
MODULE_LICENSE("GPL");
