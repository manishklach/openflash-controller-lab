/*
 * QTest coverage for the OpenFlash experimental PCI controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"

#define OF_REG_ABI_VERSION       0x0000
#define OF_REG_CAPABILITIES      0x0008
#define OF_REG_CONTROL           0x0010
#define OF_REG_STATUS            0x0014
#define OF_REG_ADMIN_SQ_LO       0x0020
#define OF_REG_ADMIN_SQ_HI       0x0024
#define OF_REG_ADMIN_CQ_LO       0x0028
#define OF_REG_ADMIN_CQ_HI       0x002c
#define OF_REG_ADMIN_QSIZE       0x0030

#define OF_ABI_VERSION           0x00000001
#define OF_CTRL_ENABLE           BIT(0)
#define OF_CTRL_RESET            BIT(1)
#define OF_STATUS_READY          BIT(0)
#define OF_REQUIRED_CAPS         (BIT(0) | BIT(1) | BIT(2) | BIT(4))

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
    f->qts = qtest_init("-machine q35 -device openflash,addr=04.0");
    f->bus = qpci_new_pc(f->qts, NULL);
    f->dev = qpci_device_find(f->bus, QPCI_DEVFN(0x4, 0));
    g_assert_nonnull(f->dev);
    qpci_device_enable(f->dev);
    f->bar = qpci_iomap(f->dev, 0, NULL);
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

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add("/openflash/identity", OpenFlashFixture, NULL,
              openflash_setup, test_identity, openflash_teardown);
    qtest_add("/openflash/enable-reset", OpenFlashFixture, NULL,
              openflash_setup, test_enable_reset, openflash_teardown);
    qtest_add("/openflash/queue-registers", OpenFlashFixture, NULL,
              openflash_setup, test_queue_registers, openflash_teardown);
    return g_test_run();
}
