// SPDX-License-Identifier: GPL-2.0-only
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "openflash.h"

static blk_status_t openflash_blk_status(u16 status)
{
	switch (status) {
	case OPENFLASH_SC_SUCCESS:
		return BLK_STS_OK;
	case OPENFLASH_SC_LBA_RANGE:
		return BLK_STS_TARGET;
	case OPENFLASH_SC_MEDIA_ERROR:
	case OPENFLASH_SC_ECC_UNCORRECTABLE:
		return BLK_STS_MEDIUM;
	case OPENFLASH_SC_INVALID_OPCODE:
	case OPENFLASH_SC_INVALID_FIELD:
		return BLK_STS_NOTSUPP;
	default:
		return BLK_STS_IOERR;
	}
}

static int openflash_drain_admin_cq(struct openflash_dev *ofdev)
{
	struct openflash_queue *queue = &ofdev->queues[0];
	struct openflash_completion *cqe;
	u16 flags;
	u16 cid;
	int completed = 0;

	for (;;) {
		cqe = &queue->cqes[queue->cq_head];
		flags = le16_to_cpu(READ_ONCE(cqe->flags));
		if ((flags & OPENFLASH_CQE_PHASE) != queue->cq_phase)
			break;
		dma_rmb();
		cid = le16_to_cpu(cqe->cid);
		if (READ_ONCE(queue->admin_pending)) {
			if (cid == queue->admin_cid) {
				queue->admin_status = le16_to_cpu(cqe->status);
				queue->admin_result = le64_to_cpu(cqe->result);
				queue->admin_error = 0;
			} else {
				queue->admin_error = -EIO;
			}
			WRITE_ONCE(queue->admin_pending, false);
			complete(&queue->admin_done);
		}

		queue->cq_head = (queue->cq_head + 1) % queue->depth;
		if (!queue->cq_head)
			queue->cq_phase ^= OPENFLASH_CQE_PHASE;
		completed++;
	}
	if (completed)
		writel(queue->cq_head,
		       ofdev->bar + OPENFLASH_CQ_HEAD_DB(queue->qid));
	return completed;
}

static irqreturn_t openflash_irq(int irq, void *data)
{
	struct openflash_dev *ofdev = data;

	if (!(readl(ofdev->bar + OPENFLASH_REG_STATUS) & OPENFLASH_STATUS_READY))
		return IRQ_NONE;
	return openflash_drain_admin_cq(ofdev) ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t openflash_io_irq(int irq, void *data)
{
	struct openflash_dev *ofdev = data;
	struct openflash_queue *queue = &ofdev->queues[1];
	struct openflash_completion *cqe;
	struct openflash_request *request;
	struct request *rq;
	u16 cid;
	u16 flags;
	int completed = 0;

	for (;;) {
		cqe = &queue->cqes[queue->cq_head];
		flags = le16_to_cpu(READ_ONCE(cqe->flags));
		if ((flags & OPENFLASH_CQE_PHASE) != queue->cq_phase)
			break;
		dma_rmb();
		cid = le16_to_cpu(cqe->cid);
		if (!cid || cid > queue->depth - 1) {
			dev_err_ratelimited(&ofdev->pdev->dev,
					    "invalid I/O completion CID %u\n", cid);
		} else {
			request = &queue->requests[cid - 1];
			rq = xchg(&request->rq, NULL);
			if (!rq) {
				dev_err_ratelimited(&ofdev->pdev->dev,
						    "stale I/O completion CID %u\n", cid);
			} else {
				while (request->nr_mapped) {
					request->nr_mapped--;
					dma_unmap_page(&ofdev->pdev->dev,
						le64_to_cpu(request->sgl[request->nr_mapped].addr),
						le32_to_cpu(request->sgl[request->nr_mapped].length),
						request->dma_dir);
				}
				blk_mq_end_request(rq,
					openflash_blk_status(le16_to_cpu(cqe->status)));
			}
		}
		queue->cq_head = (queue->cq_head + 1) % queue->depth;
		if (!queue->cq_head)
			queue->cq_phase ^= OPENFLASH_CQE_PHASE;
		completed++;
	}
	if (!completed)
		return IRQ_NONE;
	writel(queue->cq_head, ofdev->bar + OPENFLASH_CQ_HEAD_DB(queue->qid));
	return IRQ_HANDLED;
}

int openflash_setup_admin_queue(struct openflash_dev *ofdev)
{
	struct pci_dev *pdev = ofdev->pdev;
	struct openflash_queue *queue;
	size_t cq_size;
	size_t sq_size;
	u32 status;
	int ret;

	ofdev->queues = devm_kcalloc(&pdev->dev, 2, sizeof(*queue), GFP_KERNEL);
	if (!ofdev->queues)
		return -ENOMEM;
	queue = &ofdev->queues[0];
	queue->qid = 0;
	queue->depth = ofdev->queue_depth;
	queue->cq_phase = 1;
	spin_lock_init(&queue->sq_lock);
	mutex_init(&queue->admin_lock);
	init_completion(&queue->admin_done);

	sq_size = queue->depth * sizeof(*queue->sq_cmds);
	cq_size = queue->depth * sizeof(*queue->cqes);
	queue->sq_cmds = dma_alloc_coherent(&pdev->dev, sq_size,
					    &queue->sq_dma, GFP_KERNEL);
	if (!queue->sq_cmds)
		return -ENOMEM;
	queue->cqes = dma_alloc_coherent(&pdev->dev, cq_size, &queue->cq_dma, GFP_KERNEL);
	if (!queue->cqes) {
		ret = -ENOMEM;
		goto free_sq;
	}

	ret = pci_alloc_irq_vectors(pdev, 2, 2, PCI_IRQ_MSIX);
	if (ret < 0)
		goto free_cq;
	ret = request_irq(pci_irq_vector(pdev, 0), openflash_irq, 0,
			  OPENFLASH_DRV_NAME, ofdev);
	if (ret)
		goto free_vectors;

	ofdev->nr_queues = 1;
	writel(lower_32_bits(queue->sq_dma), ofdev->bar + OPENFLASH_REG_ADMIN_SQ_LO);
	writel(upper_32_bits(queue->sq_dma), ofdev->bar + OPENFLASH_REG_ADMIN_SQ_HI);
	writel(lower_32_bits(queue->cq_dma), ofdev->bar + OPENFLASH_REG_ADMIN_CQ_LO);
	writel(upper_32_bits(queue->cq_dma), ofdev->bar + OPENFLASH_REG_ADMIN_CQ_HI);
	writel(queue->depth, ofdev->bar + OPENFLASH_REG_ADMIN_QSIZE);
	/* Publish zeroed rings and DMA addresses before enabling device fetches. */
	wmb();
	writel(OPENFLASH_CTRL_ENABLE, ofdev->bar + OPENFLASH_REG_CONTROL);
	ret = readl_poll_timeout(ofdev->bar + OPENFLASH_REG_STATUS, status,
				 status & OPENFLASH_STATUS_READY, 10, 500000);
	if (ret)
		goto disable;
	return 0;

disable:
	writel(0, ofdev->bar + OPENFLASH_REG_CONTROL);
	ofdev->nr_queues = 0;
	free_irq(pci_irq_vector(pdev, 0), ofdev);
free_vectors:
	pci_free_irq_vectors(pdev);
free_cq:
	dma_free_coherent(&pdev->dev, cq_size, queue->cqes, queue->cq_dma);
free_sq:
	dma_free_coherent(&pdev->dev, sq_size, queue->sq_cmds, queue->sq_dma);
	return ret;
}

static int openflash_admin_command(struct openflash_dev *ofdev,
				   struct openflash_command *cmd, u64 *result)
{
	struct openflash_queue *queue = &ofdev->queues[0];
	u16 cid;
	u16 sq_slot;
	int ret = 0;

	mutex_lock(&queue->admin_lock);
	sq_slot = queue->sq_tail;
	cid = ++queue->next_cid;
	if (!cid)
		cid = ++queue->next_cid;
	cmd->qid = cpu_to_le16(queue->qid);
	cmd->cid = cpu_to_le16(cid);
	reinit_completion(&queue->admin_done);
	queue->admin_cid = cid;
	queue->admin_error = 0;
	WRITE_ONCE(queue->admin_pending, true);
	memcpy(&queue->sq_cmds[sq_slot], cmd, sizeof(*cmd));

	/* The command must be globally visible before publishing the new SQ tail. */
	dma_wmb();
	queue->sq_tail = (queue->sq_tail + 1) % queue->depth;
	writel(queue->sq_tail, ofdev->bar + OPENFLASH_SQ_TAIL_DB(queue->qid));

	if (!wait_for_completion_timeout(&queue->admin_done,
					 msecs_to_jiffies(500))) {
		WRITE_ONCE(queue->admin_pending, false);
		synchronize_irq(pci_irq_vector(ofdev->pdev, 0));
		ret = -ETIMEDOUT;
		goto unlock;
	}
	if (queue->admin_error) {
		ret = queue->admin_error;
		goto unlock;
	}
	if (queue->admin_status != OPENFLASH_SC_SUCCESS) {
		ret = -EIO;
		goto unlock;
	}
	if (result)
		*result = queue->admin_result;
unlock:
	mutex_unlock(&queue->admin_lock);
	return ret;
}

int openflash_admin_identify(struct openflash_dev *ofdev)
{
	struct openflash_command cmd = {
		.opcode = OPENFLASH_ADMIN_IDENTIFY,
	};
	u64 capacity;
	int ret;

	ret = openflash_admin_command(ofdev, &cmd, &capacity);
	if (ret)
		return ret;
	if (!capacity)
		return -ENODEV;
	ofdev->capacity_blocks = capacity;
	return 0;
}

static int openflash_create_io_queue(struct openflash_dev *ofdev)
{
	struct openflash_queue *queue = &ofdev->queues[1];
	struct openflash_command cmd = {
		.opcode = OPENFLASH_ADMIN_CREATE_IOQ,
	};
	u64 result;
	int ret;

	cmd.nblocks = cpu_to_le32(queue->depth);
	cmd.control = cpu_to_le32(OPENFLASH_CREATE_IOQ_CONTROL(queue->qid, 1));
	cmd.data_addr = cpu_to_le64(queue->sq_dma);
	cmd.metadata_addr = cpu_to_le64(queue->cq_dma);
	ret = openflash_admin_command(ofdev, &cmd, &result);
	if (ret)
		return ret;
	return result == queue->qid ? 0 : -EPROTO;
}

int openflash_setup_io_queue(struct openflash_dev *ofdev)
{
	struct pci_dev *pdev = ofdev->pdev;
	struct openflash_queue *queue = &ofdev->queues[1];
	size_t cq_size;
	size_t sq_size;
	u16 tag;
	int ret;

	queue->qid = 1;
	queue->depth = ofdev->queue_depth;
	queue->cq_phase = 1;
	spin_lock_init(&queue->sq_lock);
	sq_size = queue->depth * sizeof(*queue->sq_cmds);
	cq_size = queue->depth * sizeof(*queue->cqes);
	queue->sq_cmds = dma_alloc_coherent(&pdev->dev, sq_size,
					    &queue->sq_dma, GFP_KERNEL);
	if (!queue->sq_cmds)
		return -ENOMEM;
	queue->cqes = dma_alloc_coherent(&pdev->dev, cq_size, &queue->cq_dma, GFP_KERNEL);
	if (!queue->cqes) {
		ret = -ENOMEM;
		goto free_sq;
	}
	queue->requests = devm_kcalloc(&pdev->dev, queue->depth - 1,
					 sizeof(*queue->requests), GFP_KERNEL);
	if (!queue->requests) {
		ret = -ENOMEM;
		goto free_cq;
	}
	for (tag = 0; tag < queue->depth - 1; tag++) {
		queue->requests[tag].sgl = dma_alloc_coherent(&pdev->dev,
			OPENFLASH_MAX_SGL_ENTRIES * sizeof(*queue->requests[tag].sgl),
			&queue->requests[tag].sgl_dma, GFP_KERNEL);
		if (!queue->requests[tag].sgl)
			goto free_sgl;
	}
	ret = request_irq(pci_irq_vector(pdev, 1), openflash_io_irq, 0,
			  OPENFLASH_DRV_NAME "-io", ofdev);
	if (ret)
		goto free_sgl;

	ret = openflash_create_io_queue(ofdev);
	if (ret)
		goto free_irq;
	ofdev->nr_queues = 2;
	return 0;

free_irq:
	free_irq(pci_irq_vector(pdev, 1), ofdev);
free_sgl:
	while (tag--) {
		dma_free_coherent(&pdev->dev,
			OPENFLASH_MAX_SGL_ENTRIES * sizeof(*queue->requests[tag].sgl),
			queue->requests[tag].sgl, queue->requests[tag].sgl_dma);
	}
free_cq:
	dma_free_coherent(&pdev->dev, cq_size, queue->cqes, queue->cq_dma);
free_sq:
	dma_free_coherent(&pdev->dev, sq_size, queue->sq_cmds, queue->sq_dma);
	return ret;
}

int openflash_reset_controller(struct openflash_dev *ofdev)
{
	struct openflash_queue *admin = &ofdev->queues[0];
	struct openflash_queue *io = &ofdev->queues[1];
	u32 status;
	int ret;

	/* Reset removes controller queue ownership; host DMA storage stays valid. */
	memset(admin->sq_cmds, 0, admin->depth * sizeof(*admin->sq_cmds));
	memset(admin->cqes, 0, admin->depth * sizeof(*admin->cqes));
	memset(io->sq_cmds, 0, io->depth * sizeof(*io->sq_cmds));
	memset(io->cqes, 0, io->depth * sizeof(*io->cqes));
	admin->sq_tail = 0;
	admin->cq_head = 0;
	admin->cq_phase = 1;
	io->sq_tail = 0;
	io->cq_head = 0;
	io->cq_phase = 1;
	writel(OPENFLASH_CTRL_RESET, ofdev->bar + OPENFLASH_REG_CONTROL);
	readl(ofdev->bar + OPENFLASH_REG_STATUS);
	writel(lower_32_bits(admin->sq_dma), ofdev->bar + OPENFLASH_REG_ADMIN_SQ_LO);
	writel(upper_32_bits(admin->sq_dma), ofdev->bar + OPENFLASH_REG_ADMIN_SQ_HI);
	writel(lower_32_bits(admin->cq_dma), ofdev->bar + OPENFLASH_REG_ADMIN_CQ_LO);
	writel(upper_32_bits(admin->cq_dma), ofdev->bar + OPENFLASH_REG_ADMIN_CQ_HI);
	writel(admin->depth, ofdev->bar + OPENFLASH_REG_ADMIN_QSIZE);
	/* Publish cleared rings and DMA addresses before re-enabling controller fetches. */
	wmb();
	writel(OPENFLASH_CTRL_ENABLE, ofdev->bar + OPENFLASH_REG_CONTROL);
	ret = readl_poll_timeout(ofdev->bar + OPENFLASH_REG_STATUS, status,
				 status & OPENFLASH_STATUS_READY, 10, 500000);
	if (ret)
		return ret;
	return openflash_create_io_queue(ofdev);
}

void openflash_teardown_io_queue(struct openflash_dev *ofdev)
{
	struct openflash_queue *queue;
	struct pci_dev *pdev = ofdev->pdev;
	u16 tag;

	if (ofdev->nr_queues < 2)
		return;
	queue = &ofdev->queues[1];
	free_irq(pci_irq_vector(pdev, 1), ofdev);
	for (tag = 0; tag < queue->depth - 1; tag++)
		dma_free_coherent(&pdev->dev,
			OPENFLASH_MAX_SGL_ENTRIES * sizeof(*queue->requests[tag].sgl),
			queue->requests[tag].sgl, queue->requests[tag].sgl_dma);
	dma_free_coherent(&pdev->dev, queue->depth * sizeof(*queue->cqes),
			  queue->cqes, queue->cq_dma);
	dma_free_coherent(&pdev->dev, queue->depth * sizeof(*queue->sq_cmds),
			  queue->sq_cmds, queue->sq_dma);
	ofdev->nr_queues = 1;
}

void openflash_fail_io_requests(struct openflash_dev *ofdev, blk_status_t status)
{
	struct openflash_queue *queue = &ofdev->queues[1];
	struct openflash_request *request;
	struct request *rq;
	u16 tag;

	if (ofdev->nr_queues < 2)
		return;
	synchronize_irq(pci_irq_vector(ofdev->pdev, 1));
	for (tag = 0; tag < queue->depth - 1; tag++) {
		request = &queue->requests[tag];
		rq = xchg(&request->rq, NULL);
		if (!rq)
			continue;
		while (request->nr_mapped) {
			request->nr_mapped--;
			dma_unmap_page(&ofdev->pdev->dev,
				le64_to_cpu(request->sgl[request->nr_mapped].addr),
				le32_to_cpu(request->sgl[request->nr_mapped].length),
				request->dma_dir);
		}
		blk_mq_end_request(rq, status);
	}
}

void openflash_teardown_admin_queue(struct openflash_dev *ofdev)
{
	struct openflash_queue *queue;
	struct pci_dev *pdev = ofdev->pdev;

	if (!ofdev->queues)
		return;
	queue = &ofdev->queues[0];
	writel(0, ofdev->bar + OPENFLASH_REG_CONTROL);
	readl(ofdev->bar + OPENFLASH_REG_STATUS);
	free_irq(pci_irq_vector(pdev, 0), ofdev);
	pci_free_irq_vectors(pdev);
	dma_free_coherent(&pdev->dev, queue->depth * sizeof(*queue->cqes),
			  queue->cqes, queue->cq_dma);
	dma_free_coherent(&pdev->dev, queue->depth * sizeof(*queue->sq_cmds),
			  queue->sq_cmds, queue->sq_dma);
	ofdev->nr_queues = 0;
}
