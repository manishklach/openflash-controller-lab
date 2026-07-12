/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OPENFLASH_ABI_H_
#define _OPENFLASH_ABI_H_

#include <linux/bits.h>
#include <linux/types.h>

#define OPENFLASH_ABI_VERSION_MAJOR 0
#define OPENFLASH_ABI_VERSION_MINOR 2
#define OPENFLASH_ABI_VERSION \
	((OPENFLASH_ABI_VERSION_MAJOR << 16) | OPENFLASH_ABI_VERSION_MINOR)

/* BAR0 controller registers. All multi-byte fields are little-endian. */
#define OPENFLASH_REG_ABI_VERSION	0x0000
#define OPENFLASH_REG_CAPABILITIES	0x0008
#define OPENFLASH_REG_CONTROL		0x0010
#define OPENFLASH_REG_STATUS		0x0014
#define OPENFLASH_REG_ADMIN_SQ_LO	0x0020
#define OPENFLASH_REG_ADMIN_SQ_HI	0x0024
#define OPENFLASH_REG_ADMIN_CQ_LO	0x0028
#define OPENFLASH_REG_ADMIN_CQ_HI	0x002c
#define OPENFLASH_REG_ADMIN_QSIZE	0x0030
#define OPENFLASH_REG_DOORBELL_BASE	0x1000
#define OPENFLASH_REG_DOORBELL_STRIDE	0x0008
#define OPENFLASH_SQ_TAIL_DB(qid) \
	(OPENFLASH_REG_DOORBELL_BASE + (qid) * OPENFLASH_REG_DOORBELL_STRIDE)
#define OPENFLASH_CQ_HEAD_DB(qid) \
	(OPENFLASH_SQ_TAIL_DB(qid) + sizeof(u32))

#define OPENFLASH_CAP_FLUSH		BIT_ULL(0)
#define OPENFLASH_CAP_FUA		BIT_ULL(1)
#define OPENFLASH_CAP_DISCARD		BIT_ULL(2)
#define OPENFLASH_CAP_ECC_TELEMETRY	BIT_ULL(3)
#define OPENFLASH_CAP_FAULT_INJECTION	BIT_ULL(4)

#define OPENFLASH_CTRL_ENABLE		BIT(0)
#define OPENFLASH_CTRL_RESET		BIT(1)
#define OPENFLASH_STATUS_READY		BIT(0)
#define OPENFLASH_STATUS_FATAL		BIT(1)

enum openflash_opcode {
	OPENFLASH_OP_READ = 0x01,
	OPENFLASH_OP_WRITE = 0x02,
	OPENFLASH_OP_FLUSH = 0x03,
	OPENFLASH_OP_DISCARD = 0x04,
	OPENFLASH_ADMIN_IDENTIFY = 0x80,
	OPENFLASH_ADMIN_CREATE_IOQ = 0x81,
	OPENFLASH_ADMIN_DELETE_IOQ = 0x82,
	OPENFLASH_ADMIN_GET_LOG = 0x83,
};

enum openflash_status_code {
	OPENFLASH_SC_SUCCESS = 0,
	OPENFLASH_SC_INVALID_OPCODE = 1,
	OPENFLASH_SC_INVALID_FIELD = 2,
	OPENFLASH_SC_LBA_RANGE = 3,
	OPENFLASH_SC_MEDIA_ERROR = 4,
	OPENFLASH_SC_ECC_UNCORRECTABLE = 5,
	OPENFLASH_SC_ABORTED = 6,
	OPENFLASH_SC_INTERNAL = 7,
};

/* CREATE_IOQ: control[15:0] = qid, control[31:16] = MSI-X vector. */
#define OPENFLASH_CREATE_IOQ_CONTROL(qid, vector) \
	(((u32)(vector) << 16) | (u16)(qid))

/* Exactly 64 bytes; one command occupies one cache line. */
struct openflash_command {
	u8 opcode;
	u8 flags;
	__le16 qid;
	__le16 cid;
	__le16 reserved0;
	__le64 lba;
	__le32 nblocks;
	__le32 control;
	__le64 data_addr;
	__le64 metadata_addr;
	__le64 user_data;
	__le64 reserved1[2];
} __packed;

/* Exactly 64 bytes; phase is bit 0 of flags. */
struct openflash_completion {
	__le64 result;
	__le64 user_data;
	__le16 sq_head;
	__le16 cid;
	__le16 status;
	__le16 flags;
	__le64 reserved[5];
} __packed;

#define OPENFLASH_CQE_PHASE	BIT(0)

/* Write data and required metadata are durable before an FUA completion. */
#define OPENFLASH_CMD_F_FUA	BIT(0)
/* ABI v0.2: data_addr points to an SGL and control[15:0] is its entry count. */
#define OPENFLASH_CMD_F_SGL	BIT(1)
#define OPENFLASH_SGL_COUNT_MASK	GENMASK(15, 0)
#define OPENFLASH_MAX_SGL_ENTRIES	16

struct openflash_sgl_desc {
	__le64 addr;
	__le32 length;
	__le32 reserved;
} __packed;

static_assert(sizeof(struct openflash_command) == 64);
static_assert(sizeof(struct openflash_completion) == 64);
static_assert(sizeof(struct openflash_sgl_desc) == 16);

#endif /* _OPENFLASH_ABI_H_ */
