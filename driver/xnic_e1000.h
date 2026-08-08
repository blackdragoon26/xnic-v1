/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef XNIC_E1000_H
#define XNIC_E1000_H

#include <linux/bitops.h>

#define XNIC_DRV_NAME "xnic_e1000"
#define XNIC_DRV_VERSION "0.1.0"

/* Intel 8254x register subset used by this driver. */
#define XNIC_REG_CTRL       0x00000
#define XNIC_REG_STATUS     0x00008
#define XNIC_REG_EECD       0x00010
#define XNIC_REG_EERD       0x00014
#define XNIC_REG_ICR        0x000c0
#define XNIC_REG_ITR        0x000c4
#define XNIC_REG_ICS        0x000c8
#define XNIC_REG_IMS        0x000d0
#define XNIC_REG_IMC        0x000d8
#define XNIC_REG_RCTL       0x00100
#define XNIC_REG_TCTL       0x00400
#define XNIC_REG_TIPG       0x00410
#define XNIC_REG_RDBAL      0x02800
#define XNIC_REG_RDBAH      0x02804
#define XNIC_REG_RDLEN      0x02808
#define XNIC_REG_RDH        0x02810
#define XNIC_REG_RDT        0x02818
#define XNIC_REG_TDBAL      0x03800
#define XNIC_REG_TDBAH      0x03804
#define XNIC_REG_TDLEN      0x03808
#define XNIC_REG_TDH        0x03810
#define XNIC_REG_TDT        0x03818
#define XNIC_REG_MTA        0x05200
#define XNIC_REG_RAL0       0x05400
#define XNIC_REG_RAH0       0x05404

#define XNIC_CTRL_RST       BIT(26)
#define XNIC_CTRL_SLU       BIT(6)
#define XNIC_STATUS_LU      BIT(1)

#define XNIC_ICR_TXDW       BIT(0)
#define XNIC_ICR_LSC        BIT(2)
#define XNIC_ICR_RXSEQ      BIT(3)
#define XNIC_ICR_RXDMT0     BIT(4)
#define XNIC_ICR_RXO        BIT(6)
#define XNIC_ICR_RXT0       BIT(7)
#define XNIC_INT_MASK       (XNIC_ICR_TXDW | XNIC_ICR_LSC | \
			     XNIC_ICR_RXDMT0 | XNIC_ICR_RXO | XNIC_ICR_RXT0)

#define XNIC_RCTL_EN        BIT(1)
#define XNIC_RCTL_SBP       BIT(2)
#define XNIC_RCTL_UPE       BIT(3)
#define XNIC_RCTL_MPE       BIT(4)
#define XNIC_RCTL_LPE       BIT(5)
#define XNIC_RCTL_BAM       BIT(15)
#define XNIC_RCTL_BSIZE_2048 0
#define XNIC_RCTL_SECRC     BIT(26)

#define XNIC_TCTL_EN        BIT(1)
#define XNIC_TCTL_PSP       BIT(3)
#define XNIC_TCTL_CT_SHIFT  4
#define XNIC_TCTL_COLD_SHIFT 12

#define XNIC_TXD_CMD_EOP    BIT(0)
#define XNIC_TXD_CMD_IFCS   BIT(1)
#define XNIC_TXD_CMD_RS     BIT(3)
#define XNIC_TXD_STAT_DD    BIT(0)
#define XNIC_RXD_STAT_DD    BIT(0)
#define XNIC_RXD_STAT_EOP   BIT(1)

#define XNIC_RAH_AV         BIT(31)
#define XNIC_EERD_START     BIT(0)
#define XNIC_EERD_DONE      BIT(4)
#define XNIC_EERD_ADDR_SHIFT 8
#define XNIC_EERD_DATA_SHIFT 16

#define XNIC_DEFAULT_RING_COUNT 64U
#define XNIC_MIN_RING_COUNT 16U
#define XNIC_MAX_RING_COUNT 256U
#define XNIC_RX_BUF_SIZE    2048U
#define XNIC_RESET_TIMEOUT_US 20000U

struct xnic_tx_desc {
	__le64 buffer_addr;
	__le16 length;
	u8 cso;
	u8 cmd;
	u8 status;
	u8 css;
	__le16 special;
} __packed;

struct xnic_rx_desc {
	__le64 buffer_addr;
	__le16 length;
	__le16 checksum;
	u8 status;
	u8 errors;
	__le16 special;
} __packed;

#endif
