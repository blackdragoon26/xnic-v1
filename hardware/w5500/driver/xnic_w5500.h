/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef XNIC_W5500_H
#define XNIC_W5500_H

#include <linux/bitops.h>

#define XW5_DRV_NAME		"xnic_w5500"
#define XW5_DRV_VERSION		"0.1.0"

/* W5500 VDM control byte: BSB[4:0], RWB, OM[1:0] == 0. */
#define XW5_BSB_COMMON		0x00
#define XW5_BSB_SOCK_REG(n)	(0x01 + ((n) * 4))
#define XW5_BSB_S0_TX		0x02
#define XW5_BSB_S0_RX		0x03
#define XW5_CTRL(block, write)	(((block) << 3) | ((write) ? BIT(2) : 0))

/* Common register block. */
#define XW5_MR			0x0000
#define XW5_SHAR		0x0009
#define XW5_SIMR		0x0018
#define XW5_PHYCFGR		0x002e
#define XW5_VERSIONR		0x0039

#define XW5_MR_RST		BIT(7)
#define XW5_PHY_LINK		BIT(0)
#define XW5_PHY_SPEED		BIT(1)
#define XW5_PHY_DUPLEX		BIT(2)
#define XW5_VERSION		0x04

/* Socket register block. */
#define XW5_SN_MR		0x0000
#define XW5_SN_CR		0x0001
#define XW5_SN_IR		0x0002
#define XW5_SN_SR		0x0003
#define XW5_SN_RXBUF_SIZE	0x001e
#define XW5_SN_TXBUF_SIZE	0x001f
#define XW5_SN_TX_FSR		0x0020
#define XW5_SN_TX_WR		0x0024
#define XW5_SN_RX_RSR		0x0026
#define XW5_SN_RX_RD		0x0028
#define XW5_SN_IMR		0x002c

#define XW5_SN_MR_MFEN		BIT(7)
#define XW5_SN_MR_MACRAW	0x04
#define XW5_SN_CR_OPEN		0x01
#define XW5_SN_CR_CLOSE		0x10
#define XW5_SN_CR_SEND		0x20
#define XW5_SN_CR_RECV		0x40
#define XW5_SN_IR_SENDOK	BIT(4)
#define XW5_SN_IR_TIMEOUT	BIT(3)
#define XW5_SN_IR_RECV		BIT(2)
#define XW5_SN_IR_ALL		0x1f
#define XW5_SN_SR_MACRAW	0x42

#define XW5_MEM_KIB		16
#define XW5_MEM_SIZE		(XW5_MEM_KIB * 1024)
#define XW5_MEM_MASK		(XW5_MEM_SIZE - 1)
#define XW5_RX_PREFIX_LEN	2
#define XW5_TX_QUEUE_LIMIT	64
#define XW5_IRQ_RX_BUDGET	64
#define XW5_CMD_RETRIES		100
#define XW5_STABLE_RETRIES	8

#endif
