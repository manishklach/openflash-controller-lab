/*
 * QTest coverage for the OpenFlash experimental PCI controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "hw/pci/pci_regs.h"

#define OF_REG_ABI_VERSION       0x0000
#define OF_REG_CAPABILITIES      0x0008
#define OF_REG_CONTROL           0x0010
#define OF_REG_STATUS            0x0014
#define OF_REG_ADMIN_SQ_LO       0x0020
#define OF_REG_ADMIN_SQ_HI       0x0024
#define OF_REG_ADMIN_CQ_LO       0x0028
#define OF_REG_ADMIN_CQ_HI       0x002c
#define OF_REG_ADMIN_QSIZE       0x0030
#define OF_REG_DOORBELL_BASE     0x1000

#define OF_ABI_VERSION           0x00000001
#define OF_CTRL_ENABLE           BIT(0)
#define OF_CTRL_RESET            BIT(1)
#define OF_STATUS_READY          BIT(0)
#define OF_REQUIRED_CAPS         (BIT(0) | BIT(1) | BIT(2) | BIT(4))
#define OF_OP_READ               0x01
#define OF_OP_WRITE              0x02
#define OF_OP_DISCARD            0x04
#define OF_ADMIN_IDENTIFY        0x80
#define OF_SC_SUCCESS            0
#define OF_SC_INVALID_OPCODE     1
#define OF_BLOCK_SIZE            4096
#define OF_SQ_ADDR               0x00100000
#define OF_CQ_ADDR               0x00110000
#define OF_DATA_ADDR             0x00120000
#define OF_READ_ADDR             0x00121000

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

QEMU_BUILD_BUG_ON(sizeof(OpenFlashCommand) != 64);
QEMU_BUILD_BUG_ON(sizeof(OpenFlashCompletion) != 64);

typedef struct OpenFlashFixture {
    QTestState *qts;
    QPCIBus *bus;
    QPCIDevice *dev;
    QPCIBar bar;
} OpenFlashFixture;

static uint32_t openflash_readl(OpenFlashFixture *f, uint64_t offset)
{
    uint32_t value;

    qpci_memread(f->dev, f->bar, offset, &value, sizeof(value));
    return le32_to_cpu(value);
}

static void openflash_writel(OpenFlashFixture *f, uint64_t offset,
                             uint32_t value)
{
    value = cpu_to_le32(value);
    qpci_memwrite(f->dev, f->bar, offset, &value, sizeof(value));
}

static void openflash_setup(OpenFlashFixture *f, gconstpointer data)
{
    f->qts = qtest_init("-machine q35 -m 64M -device openflash,addr=04.0");
    f->bus = qpci_new_pc(f->qts, NULL);
    f->dev = qpci_device_find(f->bus, QPCI_DEVFN(0x4, 0));
    g_assert_nonnull(f->dev);
    qpci_device_enable(f->dev);
    f->bar = qpci_iomap(f->dev, 0, NULL);
}

static void openflash_configure_queue(OpenFlashFixture *f, uint32_t depth)
{
    openflash_writel(f, OF_REG_ADMIN_SQ_LO, OF_SQ_ADDR);
    openflash_writel(f, OF_REG_ADMIN_SQ_HI, 0);
    openflash_writel(f, OF_REG_ADMIN_CQ_LO, OF_CQ_ADDR);
    openflash_writel(f, OF_REG_ADMIN_CQ_HI, 0);
    openflash_writel(f, OF_REG_ADMIN_QSIZE, depth);
    openflash_writel(f, OF_REG_CONTROL, OF_CTRL_ENABLE);
}

static OpenFlashCompletion openflash_submit(OpenFlashFixture *f,
                                            OpenFlashCommand *cmd,
                                            uint16_t slot, uint16_t tail)
{
    OpenFlashCompletion cqe;

    qtest_memwrite(f->qts, OF_SQ_ADDR + slot * sizeof(*cmd), cmd, sizeof(*cmd));
    openflash_writel(f, OF_REG_DOORBELL_BASE, tail);
    qtest_memread(f->qts, OF_CQ_ADDR + slot * sizeof(cqe), &cqe, sizeof(cqe));
    return cqe;
}

static void openflash_teardown(OpenFlashFixture *f, gconstpointer data)
{
    qpci_iounmap(f->dev, f->bar);
    g_free(f->dev);
    qpci_free_pc(f->bus);
    qtest_quit(f->qts);
}

static void test_identity(OpenFlashFixture *f, gconstpointer data)
{
    uint32_t capabilities = openflash_readl(f, OF_REG_CAPABILITIES);

    g_assert_cmphex(openflash_readl(f, OF_REG_ABI_VERSION), ==,
                    OF_ABI_VERSION);
    g_assert_cmphex(capabilities & OF_REQUIRED_CAPS, ==, OF_REQUIRED_CAPS);
    g_assert_cmphex(openflash_readl(f, OF_REG_STATUS), ==, 0);
}

static void test_enable_reset(OpenFlashFixture *f, gconstpointer data)
{
    openflash_writel(f, OF_REG_CONTROL, OF_CTRL_ENABLE);
    g_assert_cmphex(openflash_readl(f, OF_REG_CONTROL), ==, OF_CTRL_ENABLE);
    g_assert_cmphex(openflash_readl(f, OF_REG_STATUS), ==, OF_STATUS_READY);

    openflash_writel(f, OF_REG_CONTROL, OF_CTRL_RESET);
    g_assert_cmphex(openflash_readl(f, OF_REG_CONTROL), ==, 0);
    g_assert_cmphex(openflash_readl(f, OF_REG_STATUS), ==, 0);
}

static void test_queue_registers(OpenFlashFixture *f, gconstpointer data)
{
    openflash_writel(f, OF_REG_ADMIN_SQ_LO, 0x12345000);
    openflash_writel(f, OF_REG_ADMIN_SQ_HI, 0x00000002);
    openflash_writel(f, OF_REG_ADMIN_CQ_LO, 0x22345000);
    openflash_writel(f, OF_REG_ADMIN_CQ_HI, 0x00000003);
    openflash_writel(f, OF_REG_ADMIN_QSIZE, 128);

    g_assert_cmphex(openflash_readl(f, OF_REG_ADMIN_SQ_LO), ==, 0x12345000);
    g_assert_cmphex(openflash_readl(f, OF_REG_ADMIN_SQ_HI), ==, 0x00000002);
    g_assert_cmphex(openflash_readl(f, OF_REG_ADMIN_CQ_LO), ==, 0x22345000);
    g_assert_cmphex(openflash_readl(f, OF_REG_ADMIN_CQ_HI), ==, 0x00000003);
    g_assert_cmpuint(openflash_readl(f, OF_REG_ADMIN_QSIZE), ==, 128);

    openflash_writel(f, OF_REG_ADMIN_QSIZE, 1);
    g_assert_cmpuint(openflash_readl(f, OF_REG_ADMIN_QSIZE), ==, 0);
}

static void test_identify_dma_msix(OpenFlashFixture *f, gconstpointer data)
{
    OpenFlashCommand cmd = {
        .opcode = OF_ADMIN_IDENTIFY,
        .cid = cpu_to_le16(7),
        .user_data = cpu_to_le64(0xcafe),
    };
    OpenFlashCompletion cqe;
    uint64_t vector_ctrl;

    qpci_msix_enable(f->dev);
    g_assert_cmpuint(qpci_msix_table_size(f->dev), ==, 1);
    vector_ctrl = f->dev->msix_table_off + PCI_MSIX_ENTRY_VECTOR_CTRL;
    qpci_io_writel(f->dev, f->dev->msix_table_bar, vector_ctrl,
                   PCI_MSIX_ENTRY_CTRL_MASKBIT);
    openflash_configure_queue(f, 8);
    cqe = openflash_submit(f, &cmd, 0, 1);

    g_assert_cmpuint(le16_to_cpu(cqe.status), ==, OF_SC_SUCCESS);
    g_assert_cmpuint(le16_to_cpu(cqe.cid), ==, 7);
    g_assert_cmpuint(le64_to_cpu(cqe.user_data), ==, 0xcafe);
    g_assert_cmpuint(le64_to_cpu(cqe.result), ==, (64 * MiB) / OF_BLOCK_SIZE);
    g_assert_true(qpci_msix_pending(f->dev, 0));
    qpci_msix_disable(f->dev);
}

static void test_data_path(OpenFlashFixture *f, gconstpointer data)
{
    uint8_t payload[OF_BLOCK_SIZE];
    uint8_t readback[OF_BLOCK_SIZE];
    OpenFlashCompletion cqe;
    OpenFlashCommand cmd = { 0 };

    memset(payload, 0xa5, sizeof(payload));
    qtest_memwrite(f->qts, OF_DATA_ADDR, payload, sizeof(payload));
    openflash_configure_queue(f, 8);

    cmd.opcode = OF_OP_WRITE;
    cmd.cid = cpu_to_le16(1);
    cmd.lba = cpu_to_le64(4);
    cmd.nblocks = cpu_to_le32(1);
    cmd.data_addr = cpu_to_le64(OF_DATA_ADDR);
    cqe = openflash_submit(f, &cmd, 0, 1);
    g_assert_cmpuint(le16_to_cpu(cqe.status), ==, OF_SC_SUCCESS);
    openflash_writel(f, OF_REG_DOORBELL_BASE + 4, 1);

    memset(&cmd, 0, sizeof(cmd));
    memset(readback, 0, sizeof(readback));
    qtest_memwrite(f->qts, OF_READ_ADDR, readback, sizeof(readback));
    cmd.opcode = OF_OP_READ;
    cmd.cid = cpu_to_le16(2);
    cmd.lba = cpu_to_le64(4);
    cmd.nblocks = cpu_to_le32(1);
    cmd.data_addr = cpu_to_le64(OF_READ_ADDR);
    cqe = openflash_submit(f, &cmd, 1, 2);
    g_assert_cmpuint(le16_to_cpu(cqe.status), ==, OF_SC_SUCCESS);
    qtest_memread(f->qts, OF_READ_ADDR, readback, sizeof(readback));
    g_assert_cmpmem(readback, sizeof(readback), payload, sizeof(payload));

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = OF_OP_DISCARD;
    cmd.cid = cpu_to_le16(3);
    cmd.lba = cpu_to_le64(4);
    cmd.nblocks = cpu_to_le32(1);
    cqe = openflash_submit(f, &cmd, 2, 3);
    g_assert_cmpuint(le16_to_cpu(cqe.status), ==, OF_SC_SUCCESS);

    memset(&cmd, 0, sizeof(cmd));
    memset(readback, 0xff, sizeof(readback));
    qtest_memwrite(f->qts, OF_READ_ADDR, readback, sizeof(readback));
    cmd.opcode = OF_OP_READ;
    cmd.cid = cpu_to_le16(4);
    cmd.lba = cpu_to_le64(4);
    cmd.nblocks = cpu_to_le32(1);
    cmd.data_addr = cpu_to_le64(OF_READ_ADDR);
    cqe = openflash_submit(f, &cmd, 3, 4);
    g_assert_cmpuint(le16_to_cpu(cqe.status), ==, OF_SC_SUCCESS);
    qtest_memread(f->qts, OF_READ_ADDR, readback, sizeof(readback));
    g_assert_true(buffer_is_zero(readback, sizeof(readback)));
}

static void test_error_and_phase_wrap(OpenFlashFixture *f, gconstpointer data)
{
    OpenFlashCommand cmd = {
        .opcode = 0xff,
        .cid = cpu_to_le16(1),
        .nblocks = cpu_to_le32(1),
    };
    OpenFlashCompletion cqe;
    int index;

    openflash_configure_queue(f, 2);
    cqe = openflash_submit(f, &cmd, 0, 1);
    g_assert_cmpuint(le16_to_cpu(cqe.status), ==, OF_SC_INVALID_OPCODE);
    g_assert_cmpuint(le16_to_cpu(cqe.flags) & 1, ==, 1);
    openflash_writel(f, OF_REG_DOORBELL_BASE + 4, 1);

    for (index = 1; index < 3; index++) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode = OF_ADMIN_IDENTIFY;
        cmd.cid = cpu_to_le16(index + 1);
        cqe = openflash_submit(f, &cmd, index & 1, (index + 1) & 1);
        g_assert_cmpuint(le16_to_cpu(cqe.status), ==, OF_SC_SUCCESS);
        openflash_writel(f, OF_REG_DOORBELL_BASE + 4, (index + 1) & 1);
    }
    g_assert_cmpuint(le16_to_cpu(cqe.flags) & 1, ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add("/openflash/identity", OpenFlashFixture, NULL,
              openflash_setup, test_identity, openflash_teardown);
    qtest_add("/openflash/enable-reset", OpenFlashFixture, NULL,
              openflash_setup, test_enable_reset, openflash_teardown);
    qtest_add("/openflash/queue-registers", OpenFlashFixture, NULL,
              openflash_setup, test_queue_registers, openflash_teardown);
    qtest_add("/openflash/identify-dma-msix", OpenFlashFixture, NULL,
              openflash_setup, test_identify_dma_msix, openflash_teardown);
    qtest_add("/openflash/data-path", OpenFlashFixture, NULL,
              openflash_setup, test_data_path, openflash_teardown);
    qtest_add("/openflash/error-phase-wrap", OpenFlashFixture, NULL,
              openflash_setup, test_error_and_phase_wrap, openflash_teardown);
    return g_test_run();
}
