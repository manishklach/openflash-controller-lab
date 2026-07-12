// SPDX-License-Identifier: GPL-2.0-only
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "openflash.h"

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
	u16 flags;
	int completed = 0;

	for (;;) {
		cqe = &queue->cqes[queue->cq_head];
		flags = le16_to_cpu(READ_ONCE(cqe->flags));
		if ((flags & OPENFLASH_CQE_PHASE) != queue->cq_phase)
			break;
		dma_rmb();
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

int openflash_setup_io_queue(struct openflash_dev *ofdev)
{
	struct pci_dev *pdev = ofdev->pdev;
	struct openflash_queue *queue = &ofdev->queues[1];
	struct openflash_command cmd = {
		.opcode = OPENFLASH_ADMIN_CREATE_IOQ,
	};
	size_t cq_size;
	size_t sq_size;
	u64 result;
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
	ret = request_irq(pci_irq_vector(pdev, 1), openflash_io_irq, 0,
			  OPENFLASH_DRV_NAME "-io", ofdev);
	if (ret)
		goto free_cq;

	cmd.nblocks = cpu_to_le32(queue->depth);
	cmd.control = cpu_to_le32(OPENFLASH_CREATE_IOQ_CONTROL(queue->qid, 1));
	cmd.data_addr = cpu_to_le64(queue->sq_dma);
	cmd.metadata_addr = cpu_to_le64(queue->cq_dma);
	ret = openflash_admin_command(ofdev, &cmd, &result);
	if (ret)
		goto free_irq;
	if (result != queue->qid) {
		ret = -EPROTO;
		goto free_irq;
	}
	ofdev->nr_queues = 2;
	return 0;

free_irq:
	free_irq(pci_irq_vector(pdev, 1), ofdev);
free_cq:
	dma_free_coherent(&pdev->dev, cq_size, queue->cqes, queue->cq_dma);
free_sq:
	dma_free_coherent(&pdev->dev, sq_size, queue->sq_cmds, queue->sq_dma);
	return ret;
}

void openflash_teardown_io_queue(struct openflash_dev *ofdev)
{
	struct openflash_queue *queue;
	struct pci_dev *pdev = ofdev->pdev;

	if (ofdev->nr_queues < 2)
		return;
	queue = &ofdev->queues[1];
	free_irq(pci_irq_vector(pdev, 1), ofdev);
	dma_free_coherent(&pdev->dev, queue->depth * sizeof(*queue->cqes),
			  queue->cqes, queue->cq_dma);
	dma_free_coherent(&pdev->dev, queue->depth * sizeof(*queue->sq_cmds),
			  queue->sq_cmds, queue->sq_dma);
	ofdev->nr_queues = 1;
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
