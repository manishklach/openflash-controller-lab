/*
 * OpenFlash experimental PCI NAND controller
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This source targets QEMU's hw/block tree and implements the OpenFlash ABI v0.2
 * admin queue as a small RAM-backed device. See qemu/README.md for integration.
 */
#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msix.h"
#include "hw/core/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qom/object.h"

#define TYPE_OPENFLASH "openflash"
OBJECT_DECLARE_SIMPLE_TYPE(OpenFlashState, OPENFLASH)

#define OF_ABI_VERSION          0x00000002
#define OF_BAR_SIZE             0x2000
#define OF_REG_ABI_VERSION      0x0000
#define OF_REG_CAPABILITIES     0x0008
#define OF_REG_CONTROL          0x0010
#define OF_REG_STATUS           0x0014
#define OF_REG_ADMIN_SQ_LO      0x0020
#define OF_REG_ADMIN_SQ_HI      0x0024
#define OF_REG_ADMIN_CQ_LO      0x0028
#define OF_REG_ADMIN_CQ_HI      0x002c
#define OF_REG_ADMIN_QSIZE      0x0030
#define OF_REG_DOORBELL_BASE    0x1000

#define OF_CTRL_ENABLE          BIT(0)
#define OF_CTRL_RESET           BIT(1)
#define OF_STATUS_READY         BIT(0)
#define OF_CAPABILITIES         (BIT_ULL(0) | BIT_ULL(1) | BIT_ULL(2) | BIT_ULL(4))

#define OF_OP_READ              0x01
#define OF_OP_WRITE             0x02
#define OF_OP_FLUSH             0x03
#define OF_OP_DISCARD           0x04
#define OF_ADMIN_IDENTIFY       0x80
#define OF_ADMIN_CREATE_IOQ     0x81

#define OF_SC_SUCCESS           0
#define OF_SC_INVALID_OPCODE    1
#define OF_SC_INVALID_FIELD     2
#define OF_SC_LBA_RANGE         3

#define OF_CMD_F_FUA            BIT(0)
#define OF_CMD_F_SGL            BIT(1)
#define OF_MAX_SGL_ENTRIES      16

#define OF_BLOCK_SIZE           4096
#define OF_DEFAULT_CAPACITY     (64 * MiB)
#define OF_MAX_QUEUES           2

typedef struct QEMU_PACKED OpenFlashCommand {
    uint8_t opcode;
    uint8_t flags;
    uint16_t qid;
    uint16_t cid;
    uint16_t reserved0;
    uint64_t lba;
    uint32_t nblocks;
    uint32_t control;
    uint64_t data_addr;
    uint64_t metadata_addr;
    uint64_t user_data;
    uint64_t reserved1[2];
} OpenFlashCommand;

typedef struct QEMU_PACKED OpenFlashCompletion {
    uint64_t result;
    uint64_t user_data;
    uint16_t sq_head;
    uint16_t cid;
    uint16_t status;
    uint16_t flags;
    uint64_t reserved[5];
} OpenFlashCompletion;

typedef struct QEMU_PACKED OpenFlashSglDesc {
    uint64_t addr;
    uint32_t length;
    uint32_t reserved;
} OpenFlashSglDesc;

QEMU_BUILD_BUG_ON(sizeof(OpenFlashCommand) != 64);
QEMU_BUILD_BUG_ON(sizeof(OpenFlashCompletion) != 64);
QEMU_BUILD_BUG_ON(sizeof(OpenFlashSglDesc) != 16);

typedef struct OpenFlashQueue {
    uint64_t sq_addr;
    uint64_t cq_addr;
    uint32_t depth;
    uint16_t sq_head;
    uint16_t cq_tail;
    uint16_t cq_head;
    uint16_t vector;
    uint8_t cq_phase;
    bool enabled;
} OpenFlashQueue;

struct OpenFlashState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    OpenFlashQueue queues[OF_MAX_QUEUES];
    uint32_t control;
    uint32_t status;
    uint64_t capacity;
    uint8_t *storage;
};

static void openflash_reset(DeviceState *dev)
{
    OpenFlashState *s = OPENFLASH(dev);

    memset(s->queues, 0, sizeof(s->queues));
    s->queues[0].cq_phase = 1;
    s->queues[0].vector = 0;
    s->control = 0;
    s->status = 0;
}

static bool openflash_transfer(OpenFlashState *s, OpenFlashCommand *cmd,
                               uint64_t offset, uint64_t length, bool write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t dma = le64_to_cpu(cmd->data_addr);
    uint64_t transferred = 0;
    uint16_t count;
    uint16_t index;

    if (!(cmd->flags & OF_CMD_F_SGL)) {
        if (!dma) {
            return false;
        }
        if (write) {
            pci_dma_read(pdev, dma, s->storage + offset, length);
        } else {
            pci_dma_write(pdev, dma, s->storage + offset, length);
        }
        return true;
    }
    count = le32_to_cpu(cmd->control) & 0xffff;
    if (!count || count > OF_MAX_SGL_ENTRIES || !dma) {
        return false;
    }
    for (index = 0; index < count; index++) {
        OpenFlashSglDesc desc = { 0 };
        uint64_t addr;
        uint32_t segment_length;

        pci_dma_read(pdev, dma + index * sizeof(desc), &desc, sizeof(desc));
        addr = le64_to_cpu(desc.addr);
        segment_length = le32_to_cpu(desc.length);
        if (!addr || !segment_length || segment_length > length - transferred) {
            return false;
        }
        if (write) {
            pci_dma_read(pdev, addr, s->storage + offset + transferred,
                         segment_length);
        } else {
            pci_dma_write(pdev, addr, s->storage + offset + transferred,
                          segment_length);
        }
        transferred += segment_length;
    }
    return transferred == length;
}

static uint16_t openflash_execute(OpenFlashState *s, OpenFlashCommand *cmd,
                                  uint64_t *result)
{
    uint64_t lba = le64_to_cpu(cmd->lba);
    uint32_t nblocks = le32_to_cpu(cmd->nblocks);
    uint64_t offset = lba * OF_BLOCK_SIZE;
    uint64_t length = (uint64_t)nblocks * OF_BLOCK_SIZE;

    *result = 0;
    if (cmd->opcode == OF_ADMIN_IDENTIFY) {
        *result = s->capacity / OF_BLOCK_SIZE;
        return OF_SC_SUCCESS;
    }
    if (cmd->opcode == OF_ADMIN_CREATE_IOQ) {
        uint32_t control = le32_to_cpu(cmd->control);
        uint16_t qid = control & 0xffff;
        uint16_t vector = control >> 16;
        OpenFlashQueue *queue;

        if (!qid || qid >= OF_MAX_QUEUES || vector >= OF_MAX_QUEUES ||
            !nblocks || nblocks > 4096 || !le64_to_cpu(cmd->data_addr) ||
            !le64_to_cpu(cmd->metadata_addr)) {
            return OF_SC_INVALID_FIELD;
        }
        queue = &s->queues[qid];
        queue->sq_addr = le64_to_cpu(cmd->data_addr);
        queue->cq_addr = le64_to_cpu(cmd->metadata_addr);
        queue->depth = nblocks;
        queue->sq_head = 0;
        queue->cq_tail = 0;
        queue->cq_head = 0;
        queue->cq_phase = 1;
        queue->vector = vector;
        queue->enabled = true;
        *result = qid;
        return OF_SC_SUCCESS;
    }
    if (cmd->opcode == OF_OP_FLUSH) {
        return OF_SC_SUCCESS;
    }
    if (!nblocks) {
        return OF_SC_INVALID_FIELD;
    }
    if (offset > s->capacity || length > s->capacity - offset) {
        return OF_SC_LBA_RANGE;
    }
    switch (cmd->opcode) {
    case OF_OP_READ:
        if (!openflash_transfer(s, cmd, offset, length, false)) {
            return OF_SC_INVALID_FIELD;
        }
        break;
    case OF_OP_WRITE:
        if (!openflash_transfer(s, cmd, offset, length, true)) {
            return OF_SC_INVALID_FIELD;
        }
        break;
    case OF_OP_DISCARD:
        memset(s->storage + offset, 0, length);
        break;
    default:
        return OF_SC_INVALID_OPCODE;
    }
    *result = nblocks;
    return OF_SC_SUCCESS;
}

static void openflash_process_sq(OpenFlashState *s, uint16_t qid,
                                 uint16_t new_tail)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    OpenFlashQueue *queue;

    if (qid >= OF_MAX_QUEUES) {
        return;
    }
    queue = &s->queues[qid];
    if (!(s->status & OF_STATUS_READY) || !queue->enabled ||
        !queue->depth || new_tail >= queue->depth) {
        return;
    }
    while (queue->sq_head != new_tail) {
        OpenFlashCommand cmd = { 0 };
        OpenFlashCompletion cqe = { 0 };
        uint64_t result;
        uint16_t status;

        pci_dma_read(pdev, queue->sq_addr + queue->sq_head * sizeof(cmd),
                     &cmd, sizeof(cmd));
        status = openflash_execute(s, &cmd, &result);
        queue->sq_head = (queue->sq_head + 1) % queue->depth;
        cqe.result = cpu_to_le64(result);
        cqe.user_data = cmd.user_data;
        cqe.sq_head = cpu_to_le16(queue->sq_head);
        cqe.cid = cmd.cid;
        cqe.status = cpu_to_le16(status);
        cqe.flags = cpu_to_le16(queue->cq_phase);
        pci_dma_write(pdev, queue->cq_addr + queue->cq_tail * sizeof(cqe),
                      &cqe, sizeof(cqe));
        queue->cq_tail = (queue->cq_tail + 1) % queue->depth;
        if (!queue->cq_tail) {
            queue->cq_phase ^= 1;
        }
    }
    if (msix_enabled(pdev)) {
        msix_notify(pdev, queue->vector);
    }
}

static uint64_t openflash_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    OpenFlashState *s = opaque;

    switch (addr) {
    case OF_REG_ABI_VERSION:  return OF_ABI_VERSION;
    case OF_REG_CAPABILITIES: return OF_CAPABILITIES;
    case OF_REG_CONTROL:      return s->control;
    case OF_REG_STATUS:       return s->status;
    case OF_REG_ADMIN_SQ_LO:  return (uint32_t)s->queues[0].sq_addr;
    case OF_REG_ADMIN_SQ_HI:  return s->queues[0].sq_addr >> 32;
    case OF_REG_ADMIN_CQ_LO:  return (uint32_t)s->queues[0].cq_addr;
    case OF_REG_ADMIN_CQ_HI:  return s->queues[0].cq_addr >> 32;
    case OF_REG_ADMIN_QSIZE:  return s->queues[0].depth;
    default:                  return 0;
    }
}

static void openflash_mmio_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned size)
{
    OpenFlashState *s = opaque;

    switch (addr) {
    case OF_REG_CONTROL:
        if (value & OF_CTRL_RESET) {
            openflash_reset(DEVICE(s));
        } else {
            s->control = value;
            s->status = value & OF_CTRL_ENABLE ? OF_STATUS_READY : 0;
        }
        break;
    case OF_REG_ADMIN_SQ_LO:
        s->queues[0].sq_addr = (s->queues[0].sq_addr & ~0xffffffffULL) | value;
        break;
    case OF_REG_ADMIN_SQ_HI:
        s->queues[0].sq_addr = (s->queues[0].sq_addr & 0xffffffffULL) | (value << 32);
        break;
    case OF_REG_ADMIN_CQ_LO:
        s->queues[0].cq_addr = (s->queues[0].cq_addr & ~0xffffffffULL) | value;
        break;
    case OF_REG_ADMIN_CQ_HI:
        s->queues[0].cq_addr = (s->queues[0].cq_addr & 0xffffffffULL) | (value << 32);
        break;
    case OF_REG_ADMIN_QSIZE:
        s->queues[0].depth = value >= 2 && value <= 4096 ? value : 0;
        s->queues[0].enabled = s->queues[0].depth != 0;
        break;
    default:
        if (addr >= OF_REG_DOORBELL_BASE &&
            addr < OF_REG_DOORBELL_BASE + OF_MAX_QUEUES * 8) {
            uint16_t qid = (addr - OF_REG_DOORBELL_BASE) / 8;
            OpenFlashQueue *queue = &s->queues[qid];

            if ((addr - OF_REG_DOORBELL_BASE) % 8 == 0) {
                openflash_process_sq(s, qid, value);
            } else if (value < queue->depth) {
                queue->cq_head = value;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "openflash: bad MMIO write @0x%" HWADDR_PRIx "\n",
                          addr);
        }
    }
}

static const MemoryRegionOps openflash_mmio_ops = {
    .read = openflash_mmio_read,
    .write = openflash_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
};

static void openflash_realize(PCIDevice *pdev, Error **errp)
{
    OpenFlashState *s = OPENFLASH(pdev);

    if (s->capacity < OF_BLOCK_SIZE || s->capacity % OF_BLOCK_SIZE) {
        error_setg(errp, "capacity must be a positive multiple of %u", OF_BLOCK_SIZE);
        return;
    }
    s->storage = g_malloc0(s->capacity);
    memory_region_init_io(&s->mmio, OBJECT(s), &openflash_mmio_ops, s,
                          "openflash-mmio", OF_BAR_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
    if (msix_init_exclusive_bar(pdev, OF_MAX_QUEUES, 4, errp)) {
        g_free(s->storage);
        s->storage = NULL;
        return;
    }
    msix_vector_use(pdev, 0);
    msix_vector_use(pdev, 1);
    openflash_reset(DEVICE(s));
}

static void openflash_exit(PCIDevice *pdev)
{
    OpenFlashState *s = OPENFLASH(pdev);

    msix_unuse_all_vectors(pdev);
    msix_uninit_exclusive_bar(pdev);
    g_free(s->storage);
}

static const Property openflash_properties[] = {
    DEFINE_PROP_SIZE("capacity", OpenFlashState, capacity, OF_DEFAULT_CAPACITY),
};

static void openflash_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    pc->realize = openflash_realize;
    pc->exit = openflash_exit;
    pc->vendor_id = 0x1d1d;
    pc->device_id = 0xf15a;
    pc->revision = 1;
    pc->class_id = PCI_CLASS_STORAGE_OTHER;
    dc->desc = "OpenFlash experimental NAND controller";
    device_class_set_props(dc, openflash_properties);
    device_class_set_legacy_reset(dc, openflash_reset);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo openflash_info = {
    .name = TYPE_OPENFLASH,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(OpenFlashState),
    .class_init = openflash_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void openflash_register_types(void)
{
    type_register_static(&openflash_info);
}
type_init(openflash_register_types)
