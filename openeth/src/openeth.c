// This is a driver for OpenCores Ethernet MAC (https://opencores.org/projects/ethmac).
// W80X chips do not use this MAC, but it is supported in QEMU
// Note that this driver is written with QEMU in mind. For example, it doesn't
// handle errors which QEMU will not report, and doesn't wait for TX to be
// finished, since QEMU does this instantly.

#include <string.h>
#include <stdlib.h>
#include "wm_heap.h"
#include "wm_error.h"
#include "wm_utils.h"
#include "wm_drv_irq.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "openeth.h"
#include "emac_opencores.h"

#define LOG_TAG "openeth"
#include "wm_log.h"

//set field of a register from variable, uses field _S & _V to determine mask
#ifndef WM_REG32_SET_FIELD
#define WM_REG32_SET_FIELD(_r, _f, _v) do {                                                                                 \
            WM_REG32_WRITE((_r),((WM_REG32_READ(_r) & ~((_f##_V) << (_f##_S)))|(((_v) & (_f##_V))<<(_f##_S))));             \
        } while(0)
#endif

#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

#ifndef MIN
#define MIN( x, y ) ( ( x ) < ( y ) ? ( x ) : ( y ) )
#endif

typedef struct {
    TaskHandle_t rx_task_hdl;
    int cur_rx_desc;
    int cur_tx_desc;
    uint8_t addr[6];
    uint8_t *rx_buf[RX_BUF_COUNT];
    uint8_t *tx_buf[TX_BUF_COUNT];

    int (*rx_data_cb)(void *priv, uint8_t *buf, uint32_t buf_len);
    void *rx_data_priv;
} emac_opencores_t;

static emac_opencores_t *g_emac_ctx = NULL;

static int emac_opencores_receive(uint8_t *buf, uint32_t *length)
{
    int ret = WM_ERR_SUCCESS;
    emac_opencores_t *emac = g_emac_ctx;

    openeth_rx_desc_t *desc_ptr = openeth_rx_desc(emac->cur_rx_desc);
    openeth_rx_desc_t desc_val = *desc_ptr;
    wm_log_debug("%s: desc %d (%p) e=%d len=%d wr=%d", __func__, emac->cur_rx_desc, desc_ptr, desc_val.e, desc_val.len, desc_val.wr);
    if (desc_val.e) {
        ret = WM_ERR_FAILED;
        goto err;
    }
    size_t rx_length = desc_val.len;
    if (*length < rx_length) {
        wm_log_error("RX length too large");
        ret = WM_ERR_INVALID_PARAM;
        goto err;
    }
    *length = rx_length;
    memcpy(buf, desc_val.rxpnt, *length);
    desc_val.e = 1;
    *desc_ptr = desc_val;

    emac->cur_rx_desc = (emac->cur_rx_desc + 1) % RX_BUF_COUNT;
    return WM_ERR_SUCCESS;
err:
    return ret;
}

// Interrupt handler and the receive task

static void emac_opencores_isr_handler(wm_irq_no_t irq, void *args)
{
    emac_opencores_t *emac = (emac_opencores_t *) args;
    BaseType_t high_task_wakeup;

    uint32_t status = WM_REG32_READ(OPENETH_INT_SOURCE_REG);

    if (status & OPENETH_INT_RXB) {
        // Notify receive task
        vTaskNotifyGiveFromISR(emac->rx_task_hdl, &high_task_wakeup);
        if (high_task_wakeup) {
            portYIELD_FROM_ISR(pdTRUE);
        }
    }

    if (status & OPENETH_INT_BUSY) {
        //wm_log_warn("%s: RX frame dropped (0x%x)", __func__, status);
    }

    // Clear interrupt
    WM_REG32_WRITE(OPENETH_INT_SOURCE_REG, status);
}

static void emac_opencores_rx_task(void *arg)
{
    //emac_opencores_t *emac = (emac_opencores_t *)arg;
    uint8_t *buffer = NULL;
    uint32_t length = 0;
    while (1) {
        if (ulTaskNotifyTake(pdFALSE, portMAX_DELAY)) {
            while (true) {
                length = ETH_MAX_PACKET_SIZE;
                buffer = malloc(length);
                if (!buffer) {
                    wm_log_error("no mem for receive buffer");
                } else if (emac_opencores_receive(buffer, &length) == WM_ERR_SUCCESS) {
                    // pass the buffer to the upper layer
                    if (length) {
                        if (g_emac_ctx->rx_data_cb) {
                            g_emac_ctx->rx_data_cb(g_emac_ctx->rx_data_priv, buffer, length);
                        }
                    }
                    //else
                    {
                        free(buffer);
                    }
                } else {
                    free(buffer);
                    break;
                }
            }
        }
    }
    vTaskDelete(NULL);
}

int emac_opencores_write_phy_reg(uint32_t phy_addr, uint32_t phy_reg, uint32_t reg_value)
{
    wm_log_debug("%s: addr=%d reg=0x%x val=0x%04x", __func__, phy_addr, phy_reg, reg_value);
    WM_REG32_SET_FIELD(OPENETH_MIIADDRESS_REG, OPENETH_FIAD, phy_addr);
    WM_REG32_SET_FIELD(OPENETH_MIIADDRESS_REG, OPENETH_RGAD, phy_reg);
    WM_REG32_WRITE(OPENETH_MIITX_DATA_REG, reg_value & OPENETH_MII_DATA_MASK);
    WM_REG32_SET_BIT(OPENETH_MIICOMMAND_REG, OPENETH_WCTRLDATA);
    return WM_ERR_SUCCESS;
}

int emac_opencores_read_phy_reg(uint32_t phy_addr, uint32_t phy_reg, uint32_t *reg_value)
{
    int ret = WM_ERR_SUCCESS;

    if (0 == reg_value) {
        wm_log_error("can't set reg_value to null");
        ret = WM_ERR_INVALID_PARAM;
        goto err;
    }

    WM_REG32_SET_FIELD(OPENETH_MIIADDRESS_REG, OPENETH_FIAD, phy_addr);
    WM_REG32_SET_FIELD(OPENETH_MIIADDRESS_REG, OPENETH_RGAD, phy_reg);
    WM_REG32_SET_BIT(OPENETH_MIICOMMAND_REG, OPENETH_RSTAT);
    *reg_value = (WM_REG32_READ(OPENETH_MIIRX_DATA_REG) & OPENETH_MII_DATA_MASK);
    wm_log_debug("%s: addr=%d reg=0x%x val=0x%04x", __func__, phy_addr, phy_reg, *reg_value);
    return WM_ERR_SUCCESS;
err:
    return ret;
}

int emac_opencores_set_addr(uint8_t *addr)
{
    wm_log_debug("%s: " MACSTR, __func__, MAC2STR(addr));
    int ret = WM_ERR_SUCCESS;

    if (NULL == addr) {
        wm_log_error("can't set mac addr to null");
        ret = WM_ERR_INVALID_PARAM;
        goto err;
    }

    emac_opencores_t *emac = g_emac_ctx;
    memcpy(emac->addr, addr, 6);
    const uint8_t mac0[4] = {addr[5], addr[4], addr[3], addr[2]};
    const uint8_t mac1[4] = {addr[1], addr[0]};
    uint32_t mac0_u32, mac1_u32;
    memcpy(&mac0_u32, &mac0, 4);
    memcpy(&mac1_u32, &mac1, 4);
    WM_REG32_WRITE(OPENETH_MAC_ADDR0_REG, mac0_u32);
    WM_REG32_WRITE(OPENETH_MAC_ADDR1_REG, mac1_u32);
    return WM_ERR_SUCCESS;
err:
    return ret;
}

int emac_opencores_get_addr(uint8_t *addr)
{
    wm_log_debug("%s: " MACSTR, __func__, MAC2STR(addr));
    int ret = WM_ERR_SUCCESS;

    if (NULL == addr) {
        wm_log_error("can't set mac addr to null");
        ret = WM_ERR_INVALID_PARAM;
        goto err;
    }

    emac_opencores_t *emac = g_emac_ctx;
    memcpy(addr, emac->addr, 6);
    return WM_ERR_SUCCESS;
err:
    return ret;
}

int emac_opencores_set_link(eth_link_t link)
{
    wm_log_debug("%s: %s", __func__, link == ETH_LINK_UP ? "up" : "down");
    int ret = WM_ERR_SUCCESS;

    switch (link) {
    case ETH_LINK_UP:
        ret  = wm_drv_irq_set_wakeup(OPENETH_INTR_SOURCE);
        ret |= wm_drv_irq_clear_pending(OPENETH_INTR_SOURCE);
        ret |= wm_drv_irq_enable(OPENETH_INTR_SOURCE);
        if (WM_ERR_SUCCESS != ret)
        {
            wm_log_error("enable interrupt failed");
            goto err;
        }
        openeth_enable();
        break;
    case ETH_LINK_DOWN:
        ret = wm_drv_irq_disable(OPENETH_INTR_SOURCE);
        if (WM_ERR_SUCCESS != ret)
        {
            wm_log_error("disable interrupt failed");
            goto err;
        }
        openeth_disable();
        break;
    default:
        wm_log_error("unknown link status");
        ret = WM_ERR_INVALID_PARAM;
        goto err;
        break;
    }
    return WM_ERR_SUCCESS;
err:
    return ret;
}

int emac_opencores_set_promiscuous(bool enable)
{
    if (enable) {
        WM_REG32_SET_BIT(OPENETH_MODER_REG, OPENETH_PRO);
    } else {
        WM_REG32_CLR_BIT(OPENETH_MODER_REG, OPENETH_PRO);
    }
    return WM_ERR_SUCCESS;
}

int emac_opencores_transmit(uint8_t *buf, uint32_t length)
{
    int ret = WM_ERR_SUCCESS;
    emac_opencores_t *emac = g_emac_ctx;

    if (length >= DMA_BUF_SIZE * TX_BUF_COUNT) {
        wm_log_error("insufficient TX buffer size");
        ret = WM_ERR_INVALID_PARAM;
        goto err;
    }

    uint32_t bytes_remaining = length;
    // In QEMU, there never is a TX operation in progress, so start with descriptor 0.

    wm_log_debug("%s: len=%d", __func__, length);
    while (bytes_remaining > 0) {
        uint32_t will_write = MIN(bytes_remaining, DMA_BUF_SIZE);
        memcpy(emac->tx_buf[emac->cur_tx_desc], buf, will_write);
        openeth_tx_desc_t *desc_ptr = openeth_tx_desc(emac->cur_tx_desc);
        openeth_tx_desc_t desc_val = *desc_ptr;
        desc_val.wr = (emac->cur_tx_desc == TX_BUF_COUNT - 1);
        desc_val.len = will_write;
        desc_val.rd = 1;
        // TXEN is already set, and this triggers a TX operation for the descriptor
        wm_log_debug("%s: desc %d (%p) len=%d wr=%d", __func__, emac->cur_tx_desc, desc_ptr, will_write, desc_val.wr);
        *desc_ptr = desc_val;
        bytes_remaining -= will_write;
        buf += will_write;
        emac->cur_tx_desc = (emac->cur_tx_desc + 1) % TX_BUF_COUNT;
    }

    return WM_ERR_SUCCESS;
err:
    return ret;
}

int emac_opencores_start(void)
{
    openeth_enable();
    return WM_ERR_SUCCESS;
}

int emac_opencores_stop(void)
{
    openeth_disable();
    return WM_ERR_SUCCESS;
}

int emac_opencores_deinit(void)
{
    emac_opencores_t *emac = g_emac_ctx;
    wm_drv_irq_detach_sw_vector(OPENETH_INTR_SOURCE);
    vTaskDelete(emac->rx_task_hdl);
    for (int i = 0; i < RX_BUF_COUNT; i++) {
        free(emac->rx_buf[i]);
    }
    for (int i = 0; i < TX_BUF_COUNT; i++) {
        free(emac->tx_buf[i]);
    }
    free(emac);
    g_emac_ctx = NULL;
    return WM_ERR_SUCCESS;
}

int emac_opencores_init(uint32_t rx_task_stack_size, uint32_t rx_task_prio)
{
    int ret;
    emac_opencores_t *emac = NULL;
    emac = calloc(1, sizeof(emac_opencores_t));

    if (!emac)
        return WM_ERR_NO_MEM;

    // Allocate DMA buffers
    for (int i = 0; i < RX_BUF_COUNT; i++) {
        emac->rx_buf[i] = wm_heap_caps_alloc(DMA_BUF_SIZE, WM_HEAP_CAP_SHARED);
        if (!(emac->rx_buf[i])) {
            ret = WM_ERR_NO_MEM;
            goto out;
        }
        openeth_init_rx_desc(openeth_rx_desc(i), emac->rx_buf[i]);
    }
    openeth_rx_desc(RX_BUF_COUNT - 1)->wr = 1;
    emac->cur_rx_desc = 0;

    for (int i = 0; i < TX_BUF_COUNT; i++) {
        emac->tx_buf[i] = wm_heap_caps_alloc(DMA_BUF_SIZE, WM_HEAP_CAP_SHARED);
        if (!(emac->tx_buf[i])) {
            ret = WM_ERR_NO_MEM;
            goto out;
        }
        openeth_init_tx_desc(openeth_tx_desc(i), emac->tx_buf[i]);
    }
    openeth_tx_desc(TX_BUF_COUNT - 1)->wr = 1;
    emac->cur_tx_desc = 0;
    // Initialize the interrupt
    ret = wm_drv_irq_attach_sw_vector(OPENETH_INTR_SOURCE, emac_opencores_isr_handler, emac);
    if (WM_ERR_SUCCESS != ret) {
        wm_log_error("attach emac interrupt failed");
        goto out;
    }

    BaseType_t xReturned = xTaskCreate(emac_opencores_rx_task, "emac_rx", rx_task_stack_size, emac,
                           rx_task_prio, &emac->rx_task_hdl);
    if (xReturned != pdPASS) {
        wm_log_error("create emac_rx task failed");
        ret = WM_ERR_NO_MEM;
        goto out;
    }

    ret = wm_sys_get_mac_addr(WM_MAC_TYPE_STA, emac->addr, 6);
    if (WM_ERR_SUCCESS != ret) {
        wm_log_error("fetch ethernet mac address failed");
        goto out;
    } else {
        //emac->addr[0] += 4;
        //printf("mac: "MACSTR"\r\n", MAC2STR(emac->addr));
    }

    // Sanity check
    if (WM_REG32_READ(OPENETH_MODER_REG) != OPENETH_MODER_DEFAULT) {
        wm_log_error("CONFIG_OPENCORES_ETH should only be used when running in QEMU.");
        ret = WM_ERR_NOT_ALLOWED;
        goto out;
    }
    // Initialize the MAC
    openeth_reset();
    openeth_set_tx_desc_cnt(TX_BUF_COUNT);
    emac_opencores_set_addr(emac->addr);

    g_emac_ctx = emac;
    return WM_ERR_SUCCESS;

out:
    if (emac) {
        if (emac->rx_task_hdl) {
            vTaskDelete(emac->rx_task_hdl);
        }
        wm_drv_irq_detach_sw_vector(OPENETH_INTR_SOURCE);
        for (int i = 0; i < TX_BUF_COUNT; i++) {
            free(emac->tx_buf[i]);
        }
        for (int i = 0; i < RX_BUF_COUNT; i++) {
            free(emac->rx_buf[i]);
        }
        free(emac);
    }
    return ret;
}

int eth_drv_set_rx_data_callback(int (*callback)(void *priv, uint8_t *buf, uint32_t buf_len), void *priv)
{
    if (!g_emac_ctx)
        return WM_ERR_NO_INITED;

    g_emac_ctx->rx_data_cb = callback;
    g_emac_ctx->rx_data_priv = priv;

    return WM_ERR_SUCCESS;
}

int eth_drv_tx(uint8_t *buf, uint32_t length)
{
    return emac_opencores_transmit(buf, length);
}