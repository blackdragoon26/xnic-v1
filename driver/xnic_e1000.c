// SPDX-License-Identifier: GPL-2.0-only
/*
 * XNIC: a deliberately small, clean-room Intel 82540EM-compatible driver.
 *
 * This is an educational driver for QEMU's e1000 model. It implements one
 * RX/TX queue, legacy descriptors, MSI/INTx, NAPI, timeout recovery, and
 * ethtool observability. It intentionally omits offloads and production NIC
 * compatibility. See docs/limitations.md before using it.
 */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/rtnetlink.h>
#include <linux/workqueue.h>

#include "xnic_e1000.h"

struct xnic_tx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	u16 len;
};

struct xnic_rx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
};

struct xnic_diag_stats {
	u64 interrupts;
	u64 spurious_interrupts;
	u64 napi_polls;
	u64 napi_budget_exhausted;
	u64 tx_ring_full;
	u64 tx_ring_wraps;
	u64 rx_ring_wraps;
	u64 tx_dma_errors;
	u64 rx_alloc_failures;
	u64 rx_dma_errors;
	u64 rx_non_eop;
	u64 rx_descriptor_errors;
	u64 resets;
	u64 tx_timeouts;
	u64 link_changes;
};

struct xnic_adapter {
	struct pci_dev *pdev;
	struct net_device *netdev;
	void __iomem *hw_addr;
	struct napi_struct napi;
	struct mutex reset_lock;
	struct work_struct reset_work;
	spinlock_t tx_lock;

	struct xnic_tx_desc *tx_desc;
	dma_addr_t tx_desc_dma;
	struct xnic_tx_buffer *tx_buf;
	u16 tx_count;
	u16 tx_next_to_use;
	u16 tx_next_to_clean;

	struct xnic_rx_desc *rx_desc;
	dma_addr_t rx_desc_dma;
	struct xnic_rx_buffer *rx_buf;
	u16 rx_count;
	u16 rx_next_to_clean;

	int irq;
	bool irq_requested;
	bool resetting;
	bool tx_stalled;
	atomic_t rx_malformed_mode;
	struct rtnl_link_stats64 stats;
	struct xnic_diag_stats diag;
};

static unsigned int ring_count = XNIC_DEFAULT_RING_COUNT;
module_param(ring_count, uint, 0444);
MODULE_PARM_DESC(ring_count, "RX/TX ring entries (power of two, 16..256)");

static int fail_probe_stage;
module_param(fail_probe_stage, int, 0644);
MODULE_PARM_DESC(fail_probe_stage, "Inject probe failure after stage 1..5");

static int fail_rx_alloc_after = -1;
module_param(fail_rx_alloc_after, int, 0644);
MODULE_PARM_DESC(fail_rx_alloc_after,
		 "One-shot RX allocation failure after N successful refills");

static inline u32 xnic_rd32(struct xnic_adapter *adapter, u32 reg)
{
	return readl(adapter->hw_addr + reg);
}

static inline void xnic_wr32(struct xnic_adapter *adapter, u32 reg, u32 value)
{
	writel(value, adapter->hw_addr + reg);
}

static inline void xnic_flush(struct xnic_adapter *adapter)
{
	(void)xnic_rd32(adapter, XNIC_REG_STATUS);
}

static u16 xnic_ring_next(u16 index, u16 count)
{
	return (index + 1) & (count - 1);
}

static void xnic_irq_disable(struct xnic_adapter *adapter)
{
	xnic_wr32(adapter, XNIC_REG_IMC, 0xffffffff);
	xnic_flush(adapter);
}

static void xnic_irq_enable(struct xnic_adapter *adapter)
{
	xnic_wr32(adapter, XNIC_REG_IMS, XNIC_INT_MASK);
	xnic_flush(adapter);
}

static int xnic_hw_reset(struct xnic_adapter *adapter)
{
	u32 ctrl;
	unsigned int elapsed;

	xnic_irq_disable(adapter);
	xnic_wr32(adapter, XNIC_REG_RCTL, 0);
	xnic_wr32(adapter, XNIC_REG_TCTL, 0);
	xnic_flush(adapter);

	ctrl = xnic_rd32(adapter, XNIC_REG_CTRL);
	xnic_wr32(adapter, XNIC_REG_CTRL, ctrl | XNIC_CTRL_RST);
	xnic_flush(adapter);

	for (elapsed = 0; elapsed < XNIC_RESET_TIMEOUT_US; elapsed += 10) {
		if (!(xnic_rd32(adapter, XNIC_REG_CTRL) & XNIC_CTRL_RST))
			break;
		udelay(10);
	}
	if (elapsed == XNIC_RESET_TIMEOUT_US)
		return -ETIMEDOUT;

	/* Reset clears interrupt state; reading ICR acknowledges pending causes. */
	xnic_irq_disable(adapter);
	(void)xnic_rd32(adapter, XNIC_REG_ICR);
	xnic_wr32(adapter, XNIC_REG_ITR, 0);
	adapter->diag.resets++;
	return 0;
}

static int xnic_read_eeprom_word(struct xnic_adapter *adapter, u8 index,
				 u16 *value)
{
	u32 eerd;
	unsigned int tries;

	xnic_wr32(adapter, XNIC_REG_EERD,
		  XNIC_EERD_START | ((u32)index << XNIC_EERD_ADDR_SHIFT));
	for (tries = 0; tries < 1000; tries++) {
		eerd = xnic_rd32(adapter, XNIC_REG_EERD);
		if (eerd & XNIC_EERD_DONE) {
			*value = (u16)(eerd >> XNIC_EERD_DATA_SHIFT);
			return 0;
		}
		udelay(5);
	}
	return -ETIMEDOUT;
}

static int xnic_read_mac(struct xnic_adapter *adapter, u8 *mac)
{
	u32 ral = xnic_rd32(adapter, XNIC_REG_RAL0);
	u32 rah = xnic_rd32(adapter, XNIC_REG_RAH0);
	u16 word;
	int i;

	mac[0] = ral;
	mac[1] = ral >> 8;
	mac[2] = ral >> 16;
	mac[3] = ral >> 24;
	mac[4] = rah;
	mac[5] = rah >> 8;
	if (is_valid_ether_addr(mac))
		return 0;

	for (i = 0; i < 3; i++) {
		if (xnic_read_eeprom_word(adapter, i, &word))
			return -EIO;
		mac[i * 2] = word;
		mac[i * 2 + 1] = word >> 8;
	}
	return is_valid_ether_addr(mac) ? 0 : -EADDRNOTAVAIL;
}

static void xnic_program_mac(struct xnic_adapter *adapter)
{
	const u8 *mac = adapter->netdev->dev_addr;
	u32 ral = mac[0] | ((u32)mac[1] << 8) | ((u32)mac[2] << 16) |
		  ((u32)mac[3] << 24);
	u32 rah = mac[4] | ((u32)mac[5] << 8) | XNIC_RAH_AV;

	xnic_wr32(adapter, XNIC_REG_RAL0, ral);
	xnic_wr32(adapter, XNIC_REG_RAH0, rah);
}

static void xnic_free_tx_buffers(struct xnic_adapter *adapter)
{
	u16 i;

	if (!adapter->tx_buf)
		return;
	for (i = 0; i < adapter->tx_count; i++) {
		if (!adapter->tx_buf[i].skb)
			continue;
		dma_unmap_single(&adapter->pdev->dev, adapter->tx_buf[i].dma,
				 adapter->tx_buf[i].len, DMA_TO_DEVICE);
		dev_kfree_skb_any(adapter->tx_buf[i].skb);
		adapter->tx_buf[i].skb = NULL;
	}
}

static void xnic_free_rx_buffers(struct xnic_adapter *adapter)
{
	u16 i;

	if (!adapter->rx_buf)
		return;
	for (i = 0; i < adapter->rx_count; i++) {
		if (!adapter->rx_buf[i].skb)
			continue;
		dma_unmap_single(&adapter->pdev->dev, adapter->rx_buf[i].dma,
				 XNIC_RX_BUF_SIZE, DMA_FROM_DEVICE);
		dev_kfree_skb_any(adapter->rx_buf[i].skb);
		adapter->rx_buf[i].skb = NULL;
	}
}

static void xnic_free_rings(struct xnic_adapter *adapter)
{
	xnic_free_tx_buffers(adapter);
	xnic_free_rx_buffers(adapter);
	kfree(adapter->tx_buf);
	adapter->tx_buf = NULL;
	kfree(adapter->rx_buf);
	adapter->rx_buf = NULL;
	if (adapter->tx_desc) {
		dma_free_coherent(&adapter->pdev->dev,
				  sizeof(*adapter->tx_desc) * adapter->tx_count,
				  adapter->tx_desc, adapter->tx_desc_dma);
		adapter->tx_desc = NULL;
	}
	if (adapter->rx_desc) {
		dma_free_coherent(&adapter->pdev->dev,
				  sizeof(*adapter->rx_desc) * adapter->rx_count,
				  adapter->rx_desc, adapter->rx_desc_dma);
		adapter->rx_desc = NULL;
	}
}

static int xnic_refill_rx_slot(struct xnic_adapter *adapter, u16 index,
			       gfp_t gfp)
{
	struct xnic_rx_buffer *buffer = &adapter->rx_buf[index];
	struct sk_buff *skb;
	dma_addr_t dma;

	if (READ_ONCE(fail_rx_alloc_after) >= 0) {
		int remaining = READ_ONCE(fail_rx_alloc_after);

		if (!remaining) {
			/* One-shot injection: recovery must rebuild the ring. */
			WRITE_ONCE(fail_rx_alloc_after, -1);
			adapter->diag.rx_alloc_failures++;
			return -ENOMEM;
		}
		WRITE_ONCE(fail_rx_alloc_after, remaining - 1);
	}
	skb = __netdev_alloc_skb(adapter->netdev,
				 XNIC_RX_BUF_SIZE + NET_IP_ALIGN, gfp);
	if (!skb) {
		adapter->diag.rx_alloc_failures++;
		return -ENOMEM;
	}
	skb_reserve(skb, NET_IP_ALIGN);
	dma = dma_map_single(&adapter->pdev->dev, skb->data,
			     XNIC_RX_BUF_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(&adapter->pdev->dev, dma)) {
		dev_kfree_skb_any(skb);
		adapter->diag.rx_dma_errors++;
		return -EIO;
	}
	buffer->skb = skb;
	buffer->dma = dma;
	adapter->rx_desc[index].buffer_addr = cpu_to_le64(dma);
	adapter->rx_desc[index].status = 0;
	return 0;
}

static int xnic_alloc_rings(struct xnic_adapter *adapter)
{
	size_t tx_size = sizeof(*adapter->tx_desc) * adapter->tx_count;
	size_t rx_size = sizeof(*adapter->rx_desc) * adapter->rx_count;
	u16 i;
	int err;

	adapter->tx_desc = dma_alloc_coherent(&adapter->pdev->dev, tx_size,
					      &adapter->tx_desc_dma, GFP_KERNEL);
	if (!adapter->tx_desc)
		return -ENOMEM;
	memset(adapter->tx_desc, 0, tx_size);
	adapter->rx_desc = dma_alloc_coherent(&adapter->pdev->dev, rx_size,
					      &adapter->rx_desc_dma, GFP_KERNEL);
	if (!adapter->rx_desc) {
		err = -ENOMEM;
		goto err_free;
	}
	memset(adapter->rx_desc, 0, rx_size);
	adapter->tx_buf = kcalloc(adapter->tx_count, sizeof(*adapter->tx_buf),
				  GFP_KERNEL);
	adapter->rx_buf = kcalloc(adapter->rx_count, sizeof(*adapter->rx_buf),
				  GFP_KERNEL);
	if (!adapter->tx_buf || !adapter->rx_buf) {
		err = -ENOMEM;
		goto err_free;
	}
	for (i = 0; i < adapter->rx_count; i++) {
		err = xnic_refill_rx_slot(adapter, i, GFP_KERNEL);
		if (err)
			goto err_free;
	}
	adapter->tx_next_to_use = 0;
	adapter->tx_next_to_clean = 0;
	adapter->rx_next_to_clean = 0;
	return 0;

err_free:
	xnic_free_rings(adapter);
	return err;
}

static void xnic_configure_rings(struct xnic_adapter *adapter)
{
	u64 tx_dma = adapter->tx_desc_dma;
	u64 rx_dma = adapter->rx_desc_dma;
	u32 tctl;

	xnic_wr32(adapter, XNIC_REG_TDBAL, lower_32_bits(tx_dma));
	xnic_wr32(adapter, XNIC_REG_TDBAH, upper_32_bits(tx_dma));
	xnic_wr32(adapter, XNIC_REG_TDLEN,
		  sizeof(*adapter->tx_desc) * adapter->tx_count);
	xnic_wr32(adapter, XNIC_REG_TDH, 0);
	xnic_wr32(adapter, XNIC_REG_TDT, 0);

	xnic_wr32(adapter, XNIC_REG_RDBAL, lower_32_bits(rx_dma));
	xnic_wr32(adapter, XNIC_REG_RDBAH, upper_32_bits(rx_dma));
	xnic_wr32(adapter, XNIC_REG_RDLEN,
		  sizeof(*adapter->rx_desc) * adapter->rx_count);
	xnic_wr32(adapter, XNIC_REG_RDH, 0);
	xnic_wr32(adapter, XNIC_REG_RDT, adapter->rx_count - 1);

	xnic_program_mac(adapter);
	xnic_wr32(adapter, XNIC_REG_RCTL,
		  XNIC_RCTL_EN | XNIC_RCTL_BAM | XNIC_RCTL_SECRC |
		  XNIC_RCTL_BSIZE_2048);
	tctl = XNIC_TCTL_EN | XNIC_TCTL_PSP | (0x10 << XNIC_TCTL_CT_SHIFT) |
	       (0x40 << XNIC_TCTL_COLD_SHIFT);
	xnic_wr32(adapter, XNIC_REG_TCTL, tctl);
	xnic_wr32(adapter, XNIC_REG_TIPG, 10 | (8 << 10) | (6 << 20));
	xnic_wr32(adapter, XNIC_REG_CTRL,
		  xnic_rd32(adapter, XNIC_REG_CTRL) | XNIC_CTRL_SLU);
	xnic_flush(adapter);
}

static unsigned int xnic_tx_unused(struct xnic_adapter *adapter)
{
	return (adapter->tx_next_to_clean - adapter->tx_next_to_use - 1) &
	       (adapter->tx_count - 1);
}

static int xnic_clean_tx(struct xnic_adapter *adapter)
{
	unsigned int cleaned = 0;
	unsigned long flags;

	spin_lock_irqsave(&adapter->tx_lock, flags);
	while (adapter->tx_next_to_clean != adapter->tx_next_to_use) {
		u16 index = adapter->tx_next_to_clean;
		struct xnic_tx_desc *desc = &adapter->tx_desc[index];
		struct xnic_tx_buffer *buffer = &adapter->tx_buf[index];

		if (!(READ_ONCE(desc->status) & XNIC_TXD_STAT_DD))
			break;
		dma_rmb();
		dma_unmap_single(&adapter->pdev->dev, buffer->dma, buffer->len,
				 DMA_TO_DEVICE);
		adapter->stats.tx_packets++;
		adapter->stats.tx_bytes += buffer->len;
		dev_consume_skb_any(buffer->skb);
		buffer->skb = NULL;
		desc->status = 0;
		adapter->tx_next_to_clean = xnic_ring_next(index,
							adapter->tx_count);
		cleaned++;
	}
	if (cleaned && netif_queue_stopped(adapter->netdev) &&
	    xnic_tx_unused(adapter) > adapter->tx_count / 4)
		netif_wake_queue(adapter->netdev);
	spin_unlock_irqrestore(&adapter->tx_lock, flags);
	return cleaned;
}

static int xnic_clean_rx(struct xnic_adapter *adapter, int budget)
{
	int work_done = 0;

	while (work_done < budget) {
		u16 index = adapter->rx_next_to_clean;
		struct xnic_rx_desc *desc = &adapter->rx_desc[index];
		struct xnic_rx_buffer *buffer = &adapter->rx_buf[index];
		struct sk_buff *skb;
		u16 length;

		if (!(READ_ONCE(desc->status) & XNIC_RXD_STAT_DD))
			break;
		dma_rmb();
		length = le16_to_cpu(desc->length);
		switch (atomic_xchg(&adapter->rx_malformed_mode, 0)) {
		case 1:
			desc->status &= ~XNIC_RXD_STAT_EOP;
			break;
		case 2:
			desc->errors = 1;
			break;
		case 3:
			length = XNIC_RX_BUF_SIZE + 1;
			break;
		default:
			break;
		}
		dma_unmap_single(&adapter->pdev->dev, buffer->dma,
				 XNIC_RX_BUF_SIZE, DMA_FROM_DEVICE);
		skb = buffer->skb;
		buffer->skb = NULL;

		if (!(desc->status & XNIC_RXD_STAT_EOP)) {
			adapter->diag.rx_non_eop++;
			adapter->stats.rx_dropped++;
			dev_kfree_skb_any(skb);
		} else if (desc->errors || length < ETH_HLEN ||
			   length > XNIC_RX_BUF_SIZE) {
			adapter->diag.rx_descriptor_errors++;
			adapter->stats.rx_errors++;
			dev_kfree_skb_any(skb);
		} else {
			skb_put(skb, length);
			skb->protocol = eth_type_trans(skb, adapter->netdev);
			skb->ip_summed = CHECKSUM_NONE;
			napi_gro_receive(&adapter->napi, skb);
			adapter->stats.rx_packets++;
			adapter->stats.rx_bytes += length;
		}

		if (xnic_refill_rx_slot(adapter, index, GFP_ATOMIC)) {
			/* Keep the descriptor unavailable; recovery work rebuilds rings. */
			desc->buffer_addr = 0;
			schedule_work(&adapter->reset_work);
			break;
		}
		dma_wmb();
		xnic_wr32(adapter, XNIC_REG_RDT, index);
		adapter->rx_next_to_clean = xnic_ring_next(index,
							adapter->rx_count);
		if (!adapter->rx_next_to_clean)
			adapter->diag.rx_ring_wraps++;
		work_done++;
	}
	return work_done;
}

static int xnic_poll(struct napi_struct *napi, int budget)
{
	struct xnic_adapter *adapter = container_of(napi, struct xnic_adapter,
						    napi);
	int work_done;

	adapter->diag.napi_polls++;
	xnic_clean_tx(adapter);
	/* The networking core may invoke a TX-only poll with budget zero. In that
	 * mode RX work and napi_complete_done() are both forbidden.
	 */
	if (!budget)
		return 0;
	work_done = xnic_clean_rx(adapter, budget);
	if (work_done == budget) {
		adapter->diag.napi_budget_exhausted++;
		return budget;
	}
	if (napi_complete_done(napi, work_done)) {
		/* IMS is set-only: enabling after completion closes the lost-wakeup
		 * window because a newly asserted cause generates another interrupt.
		 */
		xnic_irq_enable(adapter);
	}
	return work_done;
}

static irqreturn_t xnic_interrupt(int irq, void *data)
{
	struct net_device *netdev = data;
	struct xnic_adapter *adapter = netdev_priv(netdev);
	u32 cause = xnic_rd32(adapter, XNIC_REG_ICR);

	(void)irq;
	if (!cause || cause == 0xffffffff) {
		adapter->diag.spurious_interrupts++;
		return IRQ_NONE;
	}
	adapter->diag.interrupts++;
	if (cause & XNIC_ICR_LSC) {
		adapter->diag.link_changes++;
		if (xnic_rd32(adapter, XNIC_REG_STATUS) & XNIC_STATUS_LU)
			netif_carrier_on(netdev);
		else
			netif_carrier_off(netdev);
	}
	if (napi_schedule_prep(&adapter->napi)) {
		xnic_irq_disable(adapter);
		__napi_schedule(&adapter->napi);
	}
	return IRQ_HANDLED;
}

static netdev_tx_t xnic_start_xmit(struct sk_buff *skb,
				   struct net_device *netdev)
{
	struct xnic_adapter *adapter = netdev_priv(netdev);
	struct xnic_tx_desc *desc;
	struct xnic_tx_buffer *buffer;
	dma_addr_t dma;
	unsigned long flags;
	u16 index;

	if (unlikely(skb_is_nonlinear(skb)) && skb_linearize(skb)) {
		adapter->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
	if (unlikely(skb->len > ETH_FRAME_LEN)) {
		adapter->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	spin_lock_irqsave(&adapter->tx_lock, flags);
	if (!xnic_tx_unused(adapter)) {
		netif_stop_queue(netdev);
		adapter->diag.tx_ring_full++;
		spin_unlock_irqrestore(&adapter->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}
	index = adapter->tx_next_to_use;
	dma = dma_map_single(&adapter->pdev->dev, skb->data, skb->len,
			     DMA_TO_DEVICE);
	if (dma_mapping_error(&adapter->pdev->dev, dma)) {
		adapter->diag.tx_dma_errors++;
		adapter->stats.tx_dropped++;
		spin_unlock_irqrestore(&adapter->tx_lock, flags);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	buffer = &adapter->tx_buf[index];
	buffer->skb = skb;
	buffer->dma = dma;
	buffer->len = skb->len;
	desc = &adapter->tx_desc[index];
	desc->buffer_addr = cpu_to_le64(dma);
	desc->length = cpu_to_le16(skb->len);
	desc->status = 0;
	desc->cmd = XNIC_TXD_CMD_EOP | XNIC_TXD_CMD_IFCS | XNIC_TXD_CMD_RS;
	dma_wmb();
	adapter->tx_next_to_use = xnic_ring_next(index, adapter->tx_count);
	if (!adapter->tx_next_to_use)
		adapter->diag.tx_ring_wraps++;
	if (!adapter->tx_stalled)
		xnic_wr32(adapter, XNIC_REG_TDT, adapter->tx_next_to_use);
	netif_trans_update(netdev);
	if (!xnic_tx_unused(adapter)) {
		adapter->diag.tx_ring_full++;
		netif_stop_queue(netdev);
	}
	spin_unlock_irqrestore(&adapter->tx_lock, flags);
	return NETDEV_TX_OK;
}

static int xnic_up(struct xnic_adapter *adapter)
{
	int err;

	err = xnic_alloc_rings(adapter);
	if (err)
		return err;
	adapter->tx_stalled = false;
	err = xnic_hw_reset(adapter);
	if (err)
		goto err_rings;
	xnic_configure_rings(adapter);
	napi_enable(&adapter->napi);
	err = request_irq(adapter->irq, xnic_interrupt,
			  pci_dev_msi_enabled(adapter->pdev) ? 0 : IRQF_SHARED,
			  adapter->netdev->name, adapter->netdev);
	if (err)
		goto err_napi;
	adapter->irq_requested = true;
	if (xnic_rd32(adapter, XNIC_REG_STATUS) & XNIC_STATUS_LU)
		netif_carrier_on(adapter->netdev);
	else
		netif_carrier_off(adapter->netdev);
	xnic_irq_enable(adapter);
	netif_start_queue(adapter->netdev);
	return 0;

err_napi:
	napi_disable(&adapter->napi);
err_rings:
	xnic_free_rings(adapter);
	return err;
}

static void xnic_down(struct xnic_adapter *adapter)
{
	netif_tx_disable(adapter->netdev);
	xnic_irq_disable(adapter);
	if (adapter->irq_requested) {
		synchronize_irq(adapter->irq);
		free_irq(adapter->irq, adapter->netdev);
		adapter->irq_requested = false;
	}
	napi_disable(&adapter->napi);
	xnic_wr32(adapter, XNIC_REG_RCTL, 0);
	xnic_wr32(adapter, XNIC_REG_TCTL, 0);
	xnic_flush(adapter);
	netif_carrier_off(adapter->netdev);
	xnic_free_rings(adapter);
}

static int xnic_open(struct net_device *netdev)
{
	struct xnic_adapter *adapter = netdev_priv(netdev);
	int err;

	mutex_lock(&adapter->reset_lock);
	err = xnic_up(adapter);
	mutex_unlock(&adapter->reset_lock);
	return err;
}

static int xnic_stop(struct net_device *netdev)
{
	struct xnic_adapter *adapter = netdev_priv(netdev);

	mutex_lock(&adapter->reset_lock);
	xnic_down(adapter);
	mutex_unlock(&adapter->reset_lock);
	return 0;
}

static void xnic_reset_work(struct work_struct *work)
{
	struct xnic_adapter *adapter = container_of(work, struct xnic_adapter,
						    reset_work);
	int err;

	rtnl_lock();
	mutex_lock(&adapter->reset_lock);
	if (!netif_running(adapter->netdev) || adapter->resetting)
		goto out;
	adapter->resetting = true;
	netif_device_detach(adapter->netdev);
	xnic_down(adapter);
	err = xnic_up(adapter);
	if (err) {
		netdev_err(adapter->netdev, "reset recovery failed: %d\n", err);
	} else {
		netif_device_attach(adapter->netdev);
		netdev_info(adapter->netdev, "reset recovery completed\n");
	}
	adapter->resetting = false;
out:
	mutex_unlock(&adapter->reset_lock);
	rtnl_unlock();
}

static void xnic_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct xnic_adapter *adapter = netdev_priv(netdev);

	(void)txqueue;
	adapter->diag.tx_timeouts++;
	netdev_warn(netdev, "TX timeout; scheduling serialized reset\n");
	schedule_work(&adapter->reset_work);
}

static void xnic_get_stats64(struct net_device *netdev,
			     struct rtnl_link_stats64 *stats)
{
	struct xnic_adapter *adapter = netdev_priv(netdev);

	*stats = adapter->stats;
}

static const struct net_device_ops xnic_netdev_ops = {
	.ndo_open = xnic_open,
	.ndo_stop = xnic_stop,
	.ndo_start_xmit = xnic_start_xmit,
	.ndo_tx_timeout = xnic_tx_timeout,
	.ndo_get_stats64 = xnic_get_stats64,
};

static const char xnic_stat_names[][ETH_GSTRING_LEN] = {
	"interrupts", "spurious_interrupts", "napi_polls",
	"napi_budget_exhausted", "tx_ring_full", "tx_ring_wraps",
	"rx_ring_wraps", "tx_dma_errors", "rx_alloc_failures",
	"rx_dma_errors", "rx_non_eop",
	"rx_descriptor_errors", "resets", "tx_timeouts", "link_changes",
};

static void xnic_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *info)
{
	struct xnic_adapter *adapter = netdev_priv(netdev);

	strscpy(info->driver, XNIC_DRV_NAME, sizeof(info->driver));
	strscpy(info->version, XNIC_DRV_VERSION, sizeof(info->version));
	strscpy(info->bus_info, pci_name(adapter->pdev), sizeof(info->bus_info));
}

static u32 xnic_get_link(struct net_device *netdev)
{
	struct xnic_adapter *adapter = netdev_priv(netdev);

	return !!(xnic_rd32(adapter, XNIC_REG_STATUS) & XNIC_STATUS_LU);
}

static void xnic_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
{
	struct xnic_adapter *adapter = netdev_priv(netdev);

	(void)kernel_ring;
	(void)extack;
	ring->rx_max_pending = XNIC_MAX_RING_COUNT;
	ring->tx_max_pending = XNIC_MAX_RING_COUNT;
	ring->rx_pending = adapter->rx_count;
	ring->tx_pending = adapter->tx_count;
}

static int xnic_get_sset_count(struct net_device *netdev, int sset)
{
	(void)netdev;
	return sset == ETH_SS_STATS ? ARRAY_SIZE(xnic_stat_names) : -EOPNOTSUPP;
}

static void xnic_get_strings(struct net_device *netdev, u32 sset, u8 *data)
{
	(void)netdev;
	if (sset == ETH_SS_STATS)
		memcpy(data, xnic_stat_names, sizeof(xnic_stat_names));
}

static void xnic_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct xnic_adapter *adapter = netdev_priv(netdev);
	const u64 *values = (const u64 *)&adapter->diag;
	unsigned int i;

	(void)stats;
	for (i = 0; i < ARRAY_SIZE(xnic_stat_names); i++)
		data[i] = values[i];
}

static const struct ethtool_ops xnic_ethtool_ops = {
	.get_drvinfo = xnic_get_drvinfo,
	.get_link = xnic_get_link,
	.get_ringparam = xnic_get_ringparam,
	.get_sset_count = xnic_get_sset_count,
	.get_strings = xnic_get_strings,
	.get_ethtool_stats = xnic_get_ethtool_stats,
};

static int xnic_validate_ring_count(unsigned int count)
{
	return count >= XNIC_MIN_RING_COUNT && count <= XNIC_MAX_RING_COUNT &&
	       is_power_of_2(count);
}

static ssize_t xnic_reset_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct xnic_adapter *adapter;
	bool trigger;
	int err;

	(void)attr;
	err = kstrtobool(buf, &trigger);
	if (err)
		return err;
	if (!trigger)
		return -EINVAL;
	if (!netdev)
		return -ENODEV;
	adapter = netdev_priv(netdev);
	if (!netif_running(netdev))
		return -ENETDOWN;
	schedule_work(&adapter->reset_work);
	return count;
}
static DEVICE_ATTR_WO(xnic_reset);

static ssize_t xnic_tx_stall_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct xnic_adapter *adapter;
	unsigned long flags;
	bool stall;
	int err;

	(void)attr;
	err = kstrtobool(buf, &stall);
	if (err)
		return err;
	if (!netdev)
		return -ENODEV;
	adapter = netdev_priv(netdev);
	if (!netif_running(netdev))
		return -ENETDOWN;

	spin_lock_irqsave(&adapter->tx_lock, flags);
	adapter->tx_stalled = stall;
	if (!stall && adapter->tx_desc) {
		dma_wmb();
		xnic_wr32(adapter, XNIC_REG_TDT, adapter->tx_next_to_use);
	}
	spin_unlock_irqrestore(&adapter->tx_lock, flags);
	return count;
}
static DEVICE_ATTR_WO(xnic_tx_stall);

static ssize_t xnic_rx_malformed_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct xnic_adapter *adapter;
	unsigned int mode;
	int err;

	(void)attr;
	err = kstrtouint(buf, 0, &mode);
	if (err)
		return err;
	if (mode < 1 || mode > 3)
		return -EINVAL;
	if (!netdev)
		return -ENODEV;
	adapter = netdev_priv(netdev);
	if (!netif_running(netdev))
		return -ENETDOWN;
	atomic_set(&adapter->rx_malformed_mode, mode);
	return count;
}
static DEVICE_ATTR_WO(xnic_rx_malformed);

static int xnic_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net_device *netdev;
	struct xnic_adapter *adapter;
	u8 mac[ETH_ALEN];
	int err;

	(void)id;
	if (!xnic_validate_ring_count(ring_count))
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "ring_count must be power-of-two 16..256\n");
	err = pci_enable_device_mem(pdev);
	if (err)
		return err;
	if (fail_probe_stage == 1) {
		err = -EIO;
		goto err_disable;
	}
	err = pci_request_mem_regions(pdev, XNIC_DRV_NAME);
	if (err)
		goto err_disable;
	if (fail_probe_stage == 2) {
		err = -EIO;
		goto err_regions;
	}
	pci_set_master(pdev);
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err)
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		goto err_regions;
	if (fail_probe_stage == 3) {
		err = -EIO;
		goto err_regions;
	}
	netdev = alloc_etherdev(sizeof(*adapter));
	if (!netdev) {
		err = -ENOMEM;
		goto err_regions;
	}
	SET_NETDEV_DEV(netdev, &pdev->dev);
	adapter = netdev_priv(netdev);
	adapter->pdev = pdev;
	adapter->netdev = netdev;
	adapter->tx_count = ring_count;
	adapter->rx_count = ring_count;
	mutex_init(&adapter->reset_lock);
	spin_lock_init(&adapter->tx_lock);
	atomic_set(&adapter->rx_malformed_mode, 0);
	INIT_WORK(&adapter->reset_work, xnic_reset_work);
	adapter->hw_addr = pci_iomap(pdev, 0, 0);
	if (!adapter->hw_addr) {
		err = -EIO;
		goto err_netdev;
	}
	if (fail_probe_stage == 4) {
		err = -EIO;
		goto err_iounmap;
	}
	err = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_INTX);
	if (err < 0)
		goto err_iounmap;
	adapter->irq = pci_irq_vector(pdev, 0);
	if (fail_probe_stage == 5) {
		err = -EIO;
		goto err_vectors;
	}
	err = xnic_hw_reset(adapter);
	if (err)
		goto err_vectors;
	err = xnic_read_mac(adapter, mac);
	if (err)
		goto err_vectors;
	eth_hw_addr_set(netdev, mac);
	netdev->netdev_ops = &xnic_netdev_ops;
	netdev->ethtool_ops = &xnic_ethtool_ops;
	netdev->watchdog_timeo = 5 * HZ;
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = ETH_DATA_LEN;
	netdev->features = 0;
	netdev->hw_features = 0;
	netif_napi_add(netdev, &adapter->napi, xnic_poll);
	pci_set_drvdata(pdev, netdev);
	err = register_netdev(netdev);
	if (err)
		goto err_napi;
	err = device_create_file(&pdev->dev, &dev_attr_xnic_reset);
	if (err)
		goto err_unregister;
	err = device_create_file(&pdev->dev, &dev_attr_xnic_tx_stall);
	if (err)
		goto err_remove_reset;
	err = device_create_file(&pdev->dev, &dev_attr_xnic_rx_malformed);
	if (err)
		goto err_remove_tx_stall;
	dev_info(&pdev->dev, "%s %s registered as %s, MAC %pM, %s interrupt\n",
		 XNIC_DRV_NAME, XNIC_DRV_VERSION, netdev->name, netdev->dev_addr,
		 pci_dev_msi_enabled(pdev) ? "MSI" : "legacy");
	return 0;

err_remove_tx_stall:
	device_remove_file(&pdev->dev, &dev_attr_xnic_tx_stall);
err_remove_reset:
	device_remove_file(&pdev->dev, &dev_attr_xnic_reset);

err_unregister:
	unregister_netdev(netdev);

err_napi:
	netif_napi_del(&adapter->napi);
	pci_set_drvdata(pdev, NULL);
err_vectors:
	pci_free_irq_vectors(pdev);
err_iounmap:
	pci_iounmap(pdev, adapter->hw_addr);
err_netdev:
	free_netdev(netdev);
err_regions:
	pci_release_mem_regions(pdev);
err_disable:
	pci_disable_device(pdev);
	return err;
}

static void xnic_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct xnic_adapter *adapter;

	if (!netdev)
		return;
	adapter = netdev_priv(netdev);
	device_remove_file(&pdev->dev, &dev_attr_xnic_rx_malformed);
	device_remove_file(&pdev->dev, &dev_attr_xnic_tx_stall);
	device_remove_file(&pdev->dev, &dev_attr_xnic_reset);
	unregister_netdev(netdev);
	cancel_work_sync(&adapter->reset_work);
	netif_napi_del(&adapter->napi);
	xnic_hw_reset(adapter);
	pci_free_irq_vectors(pdev);
	pci_iounmap(pdev, adapter->hw_addr);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);
}

static const struct pci_device_id xnic_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x100e) }, /* 82540EM / QEMU e1000 */
	{ }
};
MODULE_DEVICE_TABLE(pci, xnic_pci_ids);

static struct pci_driver xnic_driver = {
	.name = XNIC_DRV_NAME,
	.id_table = xnic_pci_ids,
	.probe = xnic_probe,
	.remove = xnic_remove,
};
module_pci_driver(xnic_driver);

MODULE_AUTHOR("XNIC contributors");
MODULE_DESCRIPTION("Educational clean-room Intel 82540EM-compatible network driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(XNIC_DRV_VERSION);
