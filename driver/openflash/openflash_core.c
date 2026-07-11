// SPDX-License-Identifier: Apache-2.0
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "openflash.h"

/* Replace with an allocated experimental PCI ID before hardware/emulator binding. */
#define OPENFLASH_VENDOR_ID 0x1d1d
#define OPENFLASH_DEVICE_ID 0xf15a

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
	pci_set_master(pdev);
	pci_set_drvdata(pdev, ofdev);

	/* Queue negotiation, MSI-X, blk-mq, reset, and health reporting are milestone 2. */
	dev_info(&pdev->dev, "ABI %u scaffold initialized; data path disabled\n",
		 OPENFLASH_ABI_VERSION);
	return 0;
}

static void openflash_remove(struct pci_dev *pdev)
{
	struct openflash_dev *ofdev = pci_get_drvdata(pdev);

	/* Future implementation must quiesce and drain queues before resources unwind. */
	if (ofdev)
		pci_clear_master(pdev);
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
MODULE_LICENSE("Apache-2.0");

