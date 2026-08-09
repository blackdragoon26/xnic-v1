// SPDX-License-Identifier: GPL-2.0-only
/*
 * XNIC W5500 lab driver
 *
 * A deliberately small Linux net_device driver for the WIZnet W5500 in
 * Socket 0 MACRAW mode.  It is independent of the in-tree w5100 family
 * driver and binds only to the project-specific "xnic,w5500-lab" compatible.
 *
 * Synchronous SPI transfers may sleep.  RX therefore runs in a threaded IRQ
 * and TX runs in a work item; neither path performs SPI I/O from hard IRQ or
 * NAPI/softirq context.
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/property.h>
#include <linux/rtnetlink.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>

#include "xnic_w5500.h"

struct xw5_stats {
	u64 spi_errors;
	u64 command_timeouts;
	u64 unstable_reads;
	u64 malformed_rx;
	u64 rx_budget_exhausted;
	u64 tx_queue_stops;
	u64 resets;
	u64 irq_count;
};

struct xw5_priv {
	struct spi_device *spi;
	struct net_device *netdev;
	struct gpio_desc *reset_gpio;
	struct mutex hw_lock;
	struct mutex lifecycle_lock;
	spinlock_t stats_lock;
	struct sk_buff_head tx_queue;
	struct work_struct tx_work;
	struct work_struct reset_work;
	struct delayed_work link_work;
	struct delayed_work rx_retry_work;
	struct completion tx_done;
	u8 *spi_header;
	u8 *spi_data;
	struct rtnl_link_stats64 net_stats;
	struct xw5_stats diag;
	int tx_result;
	bool stopping;
	bool irq_enabled;
};

static const char xw5_stat_names[][ETH_GSTRING_LEN] = {
	"spi_errors",
	"command_timeouts",
	"unstable_reads",
	"malformed_rx",
	"rx_budget_exhausted",
	"tx_queue_stops",
	"resets",
	"irq_count",
};

static void xw5_diag_inc(struct xw5_priv *priv, u64 *counter)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->stats_lock, flags);
	(*counter)++;
	spin_unlock_irqrestore(&priv->stats_lock, flags);
}

static void xw5_net_add(struct xw5_priv *priv, u64 *counter, u64 value)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->stats_lock, flags);
	*counter += value;
	spin_unlock_irqrestore(&priv->stats_lock, flags);
}

static u16 xw5_get_be16(const u8 *bytes)
{
	return ((u16)bytes[0] << 8) | bytes[1];
}

static void xw5_put_be16(u16 value, u8 *bytes)
{
	bytes[0] = value >> 8;
	bytes[1] = value & 0xff;
}

static int xw5_xfer(struct xw5_priv *priv, u16 addr, u8 block,
		    bool write, void *data, size_t len)
{
	struct spi_transfer transfers[2] = {
		{ .tx_buf = priv->spi_header, .len = XW5_SPI_HEADER_LEN },
		{ .len = len },
	};
	int ret;

	if (!len || len > XW5_MEM_SIZE)
		return -EINVAL;

	/* hw_lock serializes reuse of these DMA-safe, device-owned buffers. */
	priv->spi_header[0] = addr >> 8;
	priv->spi_header[1] = addr & 0xff;
	priv->spi_header[2] = XW5_CTRL(block, write);
	if (write) {
		memcpy(priv->spi_data, data, len);
		transfers[1].tx_buf = priv->spi_data;
	} else {
		transfers[1].rx_buf = priv->spi_data;
	}

	ret = spi_sync_transfer(priv->spi, transfers, ARRAY_SIZE(transfers));
	if (ret) {
		xw5_diag_inc(priv, &priv->diag.spi_errors);
	} else if (!write) {
		memcpy(data, priv->spi_data, len);
	}
	return ret;
}

static int xw5_read8(struct xw5_priv *priv, u16 addr, u8 block, u8 *value)
{
	return xw5_xfer(priv, addr, block, false, value, sizeof(*value));
}

static int xw5_write8(struct xw5_priv *priv, u16 addr, u8 block, u8 value)
{
	return xw5_xfer(priv, addr, block, true, &value, sizeof(value));
}

static int xw5_read16(struct xw5_priv *priv, u16 addr, u8 block, u16 *value)
{
	u8 raw[2];
	int ret;

	ret = xw5_xfer(priv, addr, block, false, raw, sizeof(raw));
	if (!ret)
		*value = xw5_get_be16(raw);
	return ret;
}

static int xw5_write16(struct xw5_priv *priv, u16 addr, u8 block, u16 value)
{
	u8 raw[2];

	xw5_put_be16(value, raw);
	return xw5_xfer(priv, addr, block, true, raw, sizeof(raw));
}

/* Size registers can change between their two byte reads. */
static int xw5_read16_stable(struct xw5_priv *priv, u16 addr, u8 block,
			     u16 *value)
{
	u16 first, second;
	int ret, attempt;

	ret = xw5_read16(priv, addr, block, &first);
	if (ret)
		return ret;

	for (attempt = 0; attempt < XW5_STABLE_RETRIES; attempt++) {
		ret = xw5_read16(priv, addr, block, &second);
		if (ret)
			return ret;
		if (first == second) {
			*value = second;
			return 0;
		}
		first = second;
	}

	xw5_diag_inc(priv, &priv->diag.unstable_reads);
	return -EAGAIN;
}

static int xw5_buffer_xfer(struct xw5_priv *priv, u8 block, u16 pointer,
			   void *data, size_t len, bool write)
{
	u8 *bytes = data;
	size_t offset = pointer & XW5_MEM_MASK;
	size_t first = min_t(size_t, len, XW5_MEM_SIZE - offset);
	int ret;

	ret = xw5_xfer(priv, offset, block, write, bytes, first);
	if (ret || first == len)
		return ret;

	return xw5_xfer(priv, 0, block, write, bytes + first, len - first);
}

static int xw5_command(struct xw5_priv *priv, u8 command)
{
	u8 value;
	int ret, attempt;

	ret = xw5_write8(priv, XW5_SN_CR, XW5_BSB_SOCK_REG(0), command);
	if (ret)
		return ret;

	for (attempt = 0; attempt < XW5_CMD_RETRIES; attempt++) {
		ret = xw5_read8(priv, XW5_SN_CR, XW5_BSB_SOCK_REG(0), &value);
		if (ret)
			return ret;
		if (!value)
			return 0;
		usleep_range(50, 100);
	}

	xw5_diag_inc(priv, &priv->diag.command_timeouts);
	return -ETIMEDOUT;
}

static int xw5_chip_reset(struct xw5_priv *priv)
{
	int ret;

	if (priv->reset_gpio) {
		/* GPIO is active-low in DT; logical 1 asserts reset. */
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		usleep_range(600, 800);
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
		usleep_range(1000, 1500);
		return 0;
	}

	ret = xw5_write8(priv, XW5_MR, XW5_BSB_COMMON, XW5_MR_RST);
	if (!ret)
		usleep_range(1000, 1500);
	return ret;
}

static int xw5_hw_close(struct xw5_priv *priv)
{
	int ret;

	/* Mask the socket at the common level before closing it. */
	ret = xw5_write8(priv, XW5_SIMR, XW5_BSB_COMMON, 0);
	if (ret)
		return ret;
	(void)xw5_write8(priv, XW5_SN_IMR, XW5_BSB_SOCK_REG(0), 0);
	(void)xw5_write8(priv, XW5_SN_IR, XW5_BSB_SOCK_REG(0), XW5_SN_IR_ALL);
	return xw5_command(priv, XW5_SN_CR_CLOSE);
}

static int xw5_hw_open(struct xw5_priv *priv)
{
	u8 mac[ETH_ALEN];
	u8 version, status;
	int ret, socket;

	ret = xw5_chip_reset(priv);
	if (ret)
		return ret;

	ret = xw5_read8(priv, XW5_VERSIONR, XW5_BSB_COMMON, &version);
	if (ret)
		return ret;
	if (version != XW5_VERSION) {
		dev_err(&priv->spi->dev, "unexpected VERSIONR 0x%02x\n", version);
		return -ENODEV;
	}

	/* Give Socket 0 the complete 16 KiB TX and RX memories. */
	for (socket = 0; socket < 8; socket++) {
		u8 size = socket ? 0 : XW5_MEM_KIB;
		u8 block = XW5_BSB_SOCK_REG(socket);

		ret = xw5_write8(priv, XW5_SN_RXBUF_SIZE, block, size);
		if (ret)
			return ret;
		ret = xw5_write8(priv, XW5_SN_TXBUF_SIZE, block, size);
		if (ret)
			return ret;
	}

	ether_addr_copy(mac, priv->netdev->dev_addr);
	ret = xw5_xfer(priv, XW5_SHAR, XW5_BSB_COMMON, true, mac, ETH_ALEN);
	if (ret)
		return ret;

	/* Receive all MACRAW frames; eth_type_trans() applies Linux filtering. */
	ret = xw5_write8(priv, XW5_SN_MR, XW5_BSB_SOCK_REG(0),
			 XW5_SN_MR_MACRAW);
	if (ret)
		return ret;
	ret = xw5_write8(priv, XW5_SN_IR, XW5_BSB_SOCK_REG(0), XW5_SN_IR_ALL);
	if (ret)
		return ret;
	ret = xw5_write8(priv, XW5_SN_IMR, XW5_BSB_SOCK_REG(0),
			 XW5_SN_IR_RECV | XW5_SN_IR_SENDOK | XW5_SN_IR_TIMEOUT);
	if (ret)
		return ret;
	ret = xw5_write8(priv, XW5_SIMR, XW5_BSB_COMMON, BIT(0));
	if (ret)
		return ret;

	ret = xw5_command(priv, XW5_SN_CR_OPEN);
	if (ret)
		return ret;
	ret = xw5_read8(priv, XW5_SN_SR, XW5_BSB_SOCK_REG(0), &status);
	if (ret)
		return ret;
	if (status != XW5_SN_SR_MACRAW) {
		dev_err(&priv->spi->dev, "Socket 0 failed to enter MACRAW: 0x%02x\n",
			status);
		return -EIO;
	}

	return 0;
}

static void xw5_account_rx_error(struct xw5_priv *priv)
{
	xw5_net_add(priv, &priv->net_stats.rx_errors, 1);
	xw5_net_add(priv, &priv->net_stats.rx_dropped, 1);
	xw5_diag_inc(priv, &priv->diag.malformed_rx);
}

static int xw5_rx_one(struct xw5_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct sk_buff *skb;
	u8 prefix[XW5_RX_PREFIX_LEN];
	u16 available, pointer, packet_len, frame_len;
	int ret;

	ret = xw5_read16_stable(priv, XW5_SN_RX_RSR,
				XW5_BSB_SOCK_REG(0), &available);
	if (ret || !available)
		return ret;
	if (available < XW5_RX_PREFIX_LEN) {
		xw5_account_rx_error(priv);
		return -EPROTO;
	}

	ret = xw5_read16(priv, XW5_SN_RX_RD, XW5_BSB_SOCK_REG(0), &pointer);
	if (ret)
		return ret;
	ret = xw5_buffer_xfer(priv, XW5_BSB_S0_RX, pointer, prefix,
			      sizeof(prefix), false);
	if (ret)
		return ret;

	packet_len = xw5_get_be16(prefix);
	if (packet_len < XW5_RX_PREFIX_LEN + ETH_HLEN || packet_len > available ||
	    packet_len > XW5_RX_PREFIX_LEN + ETH_FRAME_LEN) {
		xw5_account_rx_error(priv);
		return -EPROTO;
	}
	frame_len = packet_len - XW5_RX_PREFIX_LEN;

	skb = netdev_alloc_skb_ip_align(netdev, frame_len);
	if (!skb) {
		xw5_net_add(priv, &priv->net_stats.rx_dropped, 1);
		return -ENOMEM;
	}

	ret = xw5_buffer_xfer(priv, XW5_BSB_S0_RX,
			      pointer + XW5_RX_PREFIX_LEN,
			      skb_put(skb, frame_len), frame_len, false);
	if (ret) {
		dev_kfree_skb_any(skb);
		return ret;
	}

	ret = xw5_write16(priv, XW5_SN_RX_RD, XW5_BSB_SOCK_REG(0),
			  pointer + packet_len);
	if (!ret)
		ret = xw5_command(priv, XW5_SN_CR_RECV);
	if (ret) {
		dev_kfree_skb_any(skb);
		return ret;
	}

	skb->protocol = eth_type_trans(skb, netdev);
	skb->ip_summed = CHECKSUM_NONE;
	xw5_net_add(priv, &priv->net_stats.rx_packets, 1);
	xw5_net_add(priv, &priv->net_stats.rx_bytes, frame_len);
	netif_rx(skb);
	return 1;
}

static int xw5_drain_rx(struct xw5_priv *priv)
{
	int ret = 0, work;

	do {
		work = 0;
		while (work < XW5_IRQ_RX_BUDGET) {
			ret = xw5_rx_one(priv);
			if (ret <= 0)
				break;
			work++;
		}
		if (work == XW5_IRQ_RX_BUDGET) {
			xw5_diag_inc(priv, &priv->diag.rx_budget_exhausted);
			cond_resched();
		}
	} while (work == XW5_IRQ_RX_BUDGET &&
		 !READ_ONCE(priv->stopping));

	return ret;
}

static bool xw5_rx_error_is_transient(int ret)
{
	return ret == -ENOMEM || ret == -EAGAIN;
}

static void xw5_handle_rx_result(struct xw5_priv *priv, int ret)
{
	if (READ_ONCE(priv->stopping))
		return;

	if (xw5_rx_error_is_transient(ret)) {
		schedule_delayed_work(&priv->rx_retry_work,
				      msecs_to_jiffies(10));
	} else if (ret < 0) {
		schedule_work(&priv->reset_work);
	}
}

static void xw5_rx_retry_work(struct work_struct *work)
{
	struct xw5_priv *priv =
		container_of(to_delayed_work(work), struct xw5_priv,
			     rx_retry_work);
	int ret;

	if (READ_ONCE(priv->stopping))
		return;

	mutex_lock(&priv->hw_lock);
	ret = xw5_drain_rx(priv);
	mutex_unlock(&priv->hw_lock);
	xw5_handle_rx_result(priv, ret);
}

static irqreturn_t xw5_irq_thread(int irq, void *data)
{
	struct net_device *netdev = data;
	struct xw5_priv *priv = netdev_priv(netdev);
	u8 status;
	int ret;

	xw5_diag_inc(priv, &priv->diag.irq_count);
	if (READ_ONCE(priv->stopping))
		return IRQ_HANDLED;

	mutex_lock(&priv->hw_lock);
	ret = xw5_read8(priv, XW5_SN_IR, XW5_BSB_SOCK_REG(0), &status);
	if (ret || !status)
		goto out;

	/* RCW1: snapshot first, then clear exactly the observed causes. */
	ret = xw5_write8(priv, XW5_SN_IR, XW5_BSB_SOCK_REG(0), status);
	if (ret)
		goto out;

	if (status & XW5_SN_IR_TIMEOUT) {
		WRITE_ONCE(priv->tx_result, -ETIMEDOUT);
		complete(&priv->tx_done);
	} else if (status & XW5_SN_IR_SENDOK) {
		WRITE_ONCE(priv->tx_result, 0);
		complete(&priv->tx_done);
	}

	if (status & XW5_SN_IR_RECV)
		ret = xw5_drain_rx(priv);
out:
	mutex_unlock(&priv->hw_lock);
	xw5_handle_rx_result(priv, ret);
	return IRQ_HANDLED;
}

static int xw5_send_one(struct xw5_priv *priv, struct sk_buff *skb)
{
	u16 free_size, pointer;
	long completed;
	int ret;

	mutex_lock(&priv->hw_lock);
	ret = xw5_read16_stable(priv, XW5_SN_TX_FSR,
				XW5_BSB_SOCK_REG(0), &free_size);
	if (ret)
		goto unlock;
	if (free_size < skb->len) {
		ret = -ENOSPC;
		goto unlock;
	}
	ret = xw5_read16(priv, XW5_SN_TX_WR, XW5_BSB_SOCK_REG(0), &pointer);
	if (ret)
		goto unlock;
	ret = xw5_buffer_xfer(priv, XW5_BSB_S0_TX, pointer,
			      skb->data, skb->len, true);
	if (ret)
		goto unlock;
	ret = xw5_write16(priv, XW5_SN_TX_WR, XW5_BSB_SOCK_REG(0),
			  pointer + skb->len);
	if (ret)
		goto unlock;

	reinit_completion(&priv->tx_done);
	WRITE_ONCE(priv->tx_result, -EINPROGRESS);
	ret = xw5_write8(priv, XW5_SN_IR, XW5_BSB_SOCK_REG(0),
			 XW5_SN_IR_SENDOK | XW5_SN_IR_TIMEOUT);
	if (!ret)
		ret = xw5_command(priv, XW5_SN_CR_SEND);
unlock:
	mutex_unlock(&priv->hw_lock);
	if (ret)
		return ret;

	netif_trans_update(priv->netdev);
	completed = wait_for_completion_timeout(&priv->tx_done, HZ);
	if (!completed)
		return -ETIMEDOUT;
	return READ_ONCE(priv->tx_result);
}

static void xw5_tx_work(struct work_struct *work)
{
	struct xw5_priv *priv = container_of(work, struct xw5_priv, tx_work);
	struct sk_buff *skb;
	int ret;

	while (!READ_ONCE(priv->stopping) &&
	       (skb = skb_dequeue(&priv->tx_queue)) != NULL) {
		ret = xw5_send_one(priv, skb);
		if (!ret) {
			xw5_net_add(priv, &priv->net_stats.tx_packets, 1);
			xw5_net_add(priv, &priv->net_stats.tx_bytes, skb->len);
		} else {
			xw5_net_add(priv, &priv->net_stats.tx_errors, 1);
			if (ret == -ETIMEDOUT)
				schedule_work(&priv->reset_work);
		}
		dev_kfree_skb_any(skb);

		if (!READ_ONCE(priv->stopping) &&
		    netif_queue_stopped(priv->netdev) &&
		    skb_queue_len(&priv->tx_queue) < XW5_TX_QUEUE_LIMIT / 2)
			netif_wake_queue(priv->netdev);
	}
}

static void xw5_drop_tx_queue(struct xw5_priv *priv)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&priv->tx_queue)) != NULL) {
		xw5_net_add(priv, &priv->net_stats.tx_dropped, 1);
		dev_kfree_skb_any(skb);
	}
}

static void xw5_link_work(struct work_struct *work)
{
	struct xw5_priv *priv =
		container_of(to_delayed_work(work), struct xw5_priv, link_work);
	u8 phy;

	if (READ_ONCE(priv->stopping))
		return;

	mutex_lock(&priv->hw_lock);
	if (!xw5_read8(priv, XW5_PHYCFGR, XW5_BSB_COMMON, &phy)) {
		if (phy & XW5_PHY_LINK)
			netif_carrier_on(priv->netdev);
		else
			netif_carrier_off(priv->netdev);
	}
	mutex_unlock(&priv->hw_lock);
	schedule_delayed_work(&priv->link_work, HZ);
}

static void xw5_reset_work(struct work_struct *work)
{
	struct xw5_priv *priv = container_of(work, struct xw5_priv, reset_work);
	struct net_device *netdev = priv->netdev;
	int ret;

	rtnl_lock();
	if (!netif_running(netdev) || READ_ONCE(priv->stopping))
		goto out_rtnl;

	mutex_lock(&priv->lifecycle_lock);
	WRITE_ONCE(priv->stopping, true);
	netif_device_detach(netdev);
	netif_stop_queue(netdev);
	complete_all(&priv->tx_done);
	if (priv->irq_enabled) {
		disable_irq(priv->spi->irq);
		priv->irq_enabled = false;
	}
	cancel_delayed_work_sync(&priv->link_work);
	cancel_delayed_work_sync(&priv->rx_retry_work);
	cancel_work_sync(&priv->tx_work);
	xw5_drop_tx_queue(priv);

	mutex_lock(&priv->hw_lock);
	ret = xw5_hw_open(priv);
	mutex_unlock(&priv->hw_lock);
	xw5_diag_inc(priv, &priv->diag.resets);

	if (!ret) {
		WRITE_ONCE(priv->stopping, false);
		enable_irq(priv->spi->irq);
		priv->irq_enabled = true;
		netif_device_attach(netdev);
		netif_wake_queue(netdev);
		schedule_delayed_work(&priv->link_work, 0);
	} else {
		netif_carrier_off(netdev);
		dev_err(&priv->spi->dev, "reset recovery failed: %d\n", ret);
	}
	mutex_unlock(&priv->lifecycle_lock);
out_rtnl:
	rtnl_unlock();
}

static netdev_tx_t xw5_start_xmit(struct sk_buff *skb,
				  struct net_device *netdev)
{
	struct xw5_priv *priv = netdev_priv(netdev);

	if (unlikely(READ_ONCE(priv->stopping)))
		return NETDEV_TX_BUSY;
	if (unlikely(skb->len > ETH_FRAME_LEN)) {
		xw5_net_add(priv, &priv->net_stats.tx_dropped, 1);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
	if (skb_put_padto(skb, ETH_ZLEN)) {
		xw5_net_add(priv, &priv->net_stats.tx_dropped, 1);
		return NETDEV_TX_OK;
	}
	if (skb_linearize(skb)) {
		xw5_net_add(priv, &priv->net_stats.tx_dropped, 1);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	skb_queue_tail(&priv->tx_queue, skb);
	if (skb_queue_len(&priv->tx_queue) >= XW5_TX_QUEUE_LIMIT) {
		netif_stop_queue(netdev);
		xw5_diag_inc(priv, &priv->diag.tx_queue_stops);
	}
	schedule_work(&priv->tx_work);
	return NETDEV_TX_OK;
}

static int xw5_open(struct net_device *netdev)
{
	struct xw5_priv *priv = netdev_priv(netdev);
	int ret;

	mutex_lock(&priv->lifecycle_lock);
	WRITE_ONCE(priv->stopping, false);
	mutex_lock(&priv->hw_lock);
	ret = xw5_hw_open(priv);
	mutex_unlock(&priv->hw_lock);
	if (ret) {
		WRITE_ONCE(priv->stopping, true);
		goto out;
	}

	enable_irq(priv->spi->irq);
	priv->irq_enabled = true;
	netif_start_queue(netdev);
	schedule_delayed_work(&priv->link_work, 0);
out:
	mutex_unlock(&priv->lifecycle_lock);
	return ret;
}

static int xw5_stop(struct net_device *netdev)
{
	struct xw5_priv *priv = netdev_priv(netdev);

	mutex_lock(&priv->lifecycle_lock);
	WRITE_ONCE(priv->stopping, true);
	netif_stop_queue(netdev);
	netif_carrier_off(netdev);
	complete_all(&priv->tx_done);
	if (priv->irq_enabled) {
		disable_irq(priv->spi->irq);
		priv->irq_enabled = false;
	}
	cancel_delayed_work_sync(&priv->link_work);
	cancel_delayed_work_sync(&priv->rx_retry_work);
	cancel_work_sync(&priv->tx_work);
	xw5_drop_tx_queue(priv);
	mutex_lock(&priv->hw_lock);
	(void)xw5_hw_close(priv);
	mutex_unlock(&priv->hw_lock);
	mutex_unlock(&priv->lifecycle_lock);
	return 0;
}

static void xw5_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct xw5_priv *priv = netdev_priv(netdev);

	xw5_net_add(priv, &priv->net_stats.tx_errors, 1);
	schedule_work(&priv->reset_work);
}

static void xw5_get_stats64(struct net_device *netdev,
			    struct rtnl_link_stats64 *stats)
{
	struct xw5_priv *priv = netdev_priv(netdev);
	unsigned long flags;

	spin_lock_irqsave(&priv->stats_lock, flags);
	*stats = priv->net_stats;
	spin_unlock_irqrestore(&priv->stats_lock, flags);
}

static int xw5_set_mac_address(struct net_device *netdev, void *address)
{
	if (netif_running(netdev))
		return -EBUSY;
	return eth_mac_addr(netdev, address);
}

static const struct net_device_ops xw5_netdev_ops = {
	.ndo_open		= xw5_open,
	.ndo_stop		= xw5_stop,
	.ndo_start_xmit		= xw5_start_xmit,
	.ndo_tx_timeout		= xw5_tx_timeout,
	.ndo_get_stats64	= xw5_get_stats64,
	.ndo_set_mac_address	= xw5_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
};

static void xw5_get_drvinfo(struct net_device *netdev,
			   struct ethtool_drvinfo *info)
{
	strscpy(info->driver, XW5_DRV_NAME, sizeof(info->driver));
	strscpy(info->version, XW5_DRV_VERSION, sizeof(info->version));
	strscpy(info->bus_info, dev_name(netdev->dev.parent),
		sizeof(info->bus_info));
}

static int xw5_get_sset_count(struct net_device *netdev, int sset)
{
	return sset == ETH_SS_STATS ? ARRAY_SIZE(xw5_stat_names) : -EOPNOTSUPP;
}

static void xw5_get_strings(struct net_device *netdev, u32 sset, u8 *data)
{
	if (sset == ETH_SS_STATS)
		memcpy(data, xw5_stat_names, sizeof(xw5_stat_names));
}

static void xw5_get_ethtool_stats(struct net_device *netdev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct xw5_priv *priv = netdev_priv(netdev);
	unsigned long flags;

	spin_lock_irqsave(&priv->stats_lock, flags);
	data[0] = priv->diag.spi_errors;
	data[1] = priv->diag.command_timeouts;
	data[2] = priv->diag.unstable_reads;
	data[3] = priv->diag.malformed_rx;
	data[4] = priv->diag.rx_budget_exhausted;
	data[5] = priv->diag.tx_queue_stops;
	data[6] = priv->diag.resets;
	data[7] = priv->diag.irq_count;
	spin_unlock_irqrestore(&priv->stats_lock, flags);
}

static const struct ethtool_ops xw5_ethtool_ops = {
	.get_drvinfo		= xw5_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_sset_count		= xw5_get_sset_count,
	.get_strings		= xw5_get_strings,
	.get_ethtool_stats	= xw5_get_ethtool_stats,
};

static ssize_t force_reset_store(struct device *device,
				 struct device_attribute *attr,
				 const char *buffer, size_t count)
{
	struct spi_device *spi = to_spi_device(device);
	struct net_device *netdev = spi_get_drvdata(spi);
	struct xw5_priv *priv;

	if (!netdev)
		return -ENODEV;
	if (!sysfs_streq(buffer, "1"))
		return -EINVAL;
	if (!netif_running(netdev))
		return -ENETDOWN;

	priv = netdev_priv(netdev);
	schedule_work(&priv->reset_work);
	return count;
}
static DEVICE_ATTR_WO(force_reset);

static int xw5_probe(struct spi_device *spi)
{
	struct net_device *netdev;
	struct xw5_priv *priv;
	u8 mac[ETH_ALEN];
	int ret;

	if (spi->irq <= 0)
		return dev_err_probe(&spi->dev, -EINVAL,
				     "an active-low interrupt is required\n");

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "SPI setup failed\n");

	netdev = alloc_etherdev(sizeof(*priv));
	if (!netdev)
		return -ENOMEM;
	SET_NETDEV_DEV(netdev, &spi->dev);
	priv = netdev_priv(netdev);
	priv->spi = spi;
	priv->netdev = netdev;
	mutex_init(&priv->hw_lock);
	mutex_init(&priv->lifecycle_lock);
	spin_lock_init(&priv->stats_lock);
	skb_queue_head_init(&priv->tx_queue);
	INIT_WORK(&priv->tx_work, xw5_tx_work);
	INIT_WORK(&priv->reset_work, xw5_reset_work);
	INIT_DELAYED_WORK(&priv->link_work, xw5_link_work);
	INIT_DELAYED_WORK(&priv->rx_retry_work, xw5_rx_retry_work);
	init_completion(&priv->tx_done);
	WRITE_ONCE(priv->stopping, true);
	priv->spi_header = devm_kmalloc(&spi->dev, XW5_SPI_HEADER_LEN,
					GFP_KERNEL);
	priv->spi_data = devm_kmalloc(&spi->dev, XW5_MEM_SIZE, GFP_KERNEL);
	if (!priv->spi_header || !priv->spi_data) {
		ret = -ENOMEM;
		goto free_netdev;
	}

	priv->reset_gpio = devm_gpiod_get_optional(&spi->dev, "reset",
						    GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset_gpio)) {
		ret = dev_err_probe(&spi->dev, PTR_ERR(priv->reset_gpio),
				    "reset GPIO unavailable\n");
		goto free_netdev;
	}

	if (!device_property_read_u8_array(&spi->dev, "local-mac-address",
					   mac, ETH_ALEN) && is_valid_ether_addr(mac))
		eth_hw_addr_set(netdev, mac);
	else
		eth_hw_addr_random(netdev);

	netdev->netdev_ops = &xw5_netdev_ops;
	netdev->ethtool_ops = &xw5_ethtool_ops;
	netdev->watchdog_timeo = 2 * HZ;
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = ETH_DATA_LEN;
	netdev->flags |= IFF_MULTICAST;
	netif_carrier_off(netdev);

	ret = devm_request_threaded_irq(&spi->dev, spi->irq, NULL,
					xw5_irq_thread,
					IRQF_ONESHOT | IRQF_TRIGGER_LOW,
					dev_name(&spi->dev), netdev);
	if (ret) {
		ret = dev_err_probe(&spi->dev, ret, "IRQ request failed\n");
		goto free_netdev;
	}
	disable_irq(spi->irq);

	spi_set_drvdata(spi, netdev);
	ret = register_netdev(netdev);
	if (ret)
		goto free_netdev;

	ret = device_create_file(&spi->dev, &dev_attr_force_reset);
	if (ret)
		goto unregister_netdev;

	dev_info(&spi->dev, "%s registered as %s at %u Hz\n",
		 XW5_DRV_NAME, netdev->name, spi->max_speed_hz);
	return 0;

unregister_netdev:
	unregister_netdev(netdev);
free_netdev:
	spi_set_drvdata(spi, NULL);
	free_netdev(netdev);
	return ret;
}

static void xw5_remove(struct spi_device *spi)
{
	struct net_device *netdev = spi_get_drvdata(spi);
	struct xw5_priv *priv = netdev_priv(netdev);

	device_remove_file(&spi->dev, &dev_attr_force_reset);
	WRITE_ONCE(priv->stopping, true);
	cancel_work_sync(&priv->reset_work);
	unregister_netdev(netdev);
	cancel_work_sync(&priv->tx_work);
	cancel_delayed_work_sync(&priv->link_work);
	cancel_delayed_work_sync(&priv->rx_retry_work);
	xw5_drop_tx_queue(priv);
	spi_set_drvdata(spi, NULL);
	free_netdev(netdev);
}

static const struct of_device_id xw5_of_match[] = {
	{ .compatible = "xnic,w5500-lab" },
	{ }
};
MODULE_DEVICE_TABLE(of, xw5_of_match);

static const struct spi_device_id xw5_ids[] = {
	{ "w5500-lab", 0 },
	{ "xnic-w5500-lab", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, xw5_ids);

static struct spi_driver xw5_driver = {
	.driver = {
		.name = XW5_DRV_NAME,
		.of_match_table = xw5_of_match,
	},
	.probe = xw5_probe,
	.remove = xw5_remove,
	.id_table = xw5_ids,
};
module_spi_driver(xw5_driver);

MODULE_AUTHOR("Sankalp Jha");
MODULE_DESCRIPTION("Clean-room W5500 MACRAW SPI Ethernet lab driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(XW5_DRV_VERSION);
