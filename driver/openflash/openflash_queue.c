// SPDX-License-Identifier: GPL-2.0-only
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "openflash.h"

static irqreturn_t openflash_irq(int irq, void *data)
{
	struct openflash_dev *ofdev = data;

	/* Completion draining is enabled when admin command submission lands. */
	if (!(readl(ofdev->bar + OPENFLASH_REG_STATUS) & OPENFLASH_STATUS_READY))
		return IRQ_NONE;
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

	queue = devm_kzalloc(&pdev->dev, sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return -ENOMEM;
	queue->qid = 0;
	queue->depth = ofdev->queue_depth;
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

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSIX);
	if (ret < 0)
		goto free_cq;
	ret = request_irq(pci_irq_vector(pdev, 0), openflash_irq, 0,
			  OPENFLASH_DRV_NAME, ofdev);
	if (ret)
		goto free_vectors;

	ofdev->queues = queue;
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
	ofdev->queues = NULL;
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

void openflash_teardown_admin_queue(struct openflash_dev *ofdev)
{
	struct openflash_queue *queue = ofdev->queues;
	struct pci_dev *pdev = ofdev->pdev;

	if (!queue)
		return;
	writel(0, ofdev->bar + OPENFLASH_REG_CONTROL);
	readl(ofdev->bar + OPENFLASH_REG_STATUS);
	free_irq(pci_irq_vector(pdev, 0), ofdev);
	pci_free_irq_vectors(pdev);
	dma_free_coherent(&pdev->dev, queue->depth * sizeof(*queue->cqes),
			  queue->cqes, queue->cq_dma);
	dma_free_coherent(&pdev->dev, queue->depth * sizeof(*queue->sq_cmds),
			  queue->sq_cmds, queue->sq_dma);
	ofdev->queues = NULL;
	ofdev->nr_queues = 0;
}
