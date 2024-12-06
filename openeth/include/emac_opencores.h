#ifndef __EMAC_OPENCORES_H__
#define __EMAC_OPENCORES_H__

#ifdef __cplusplus
extern "C" {
#endif

#define ETH_HEADER_LEN      (14) /* Ethernet frame header size: Dest addr(6 Bytes) + Src addr(6 Bytes) + length/type(2 Bytes) */
#define ETH_VLAN_TAG_LEN    (4)  /* Optional 802.1q VLAN Tag length */
#define ETH_CRC_LEN         (4)  /* Ethernet frame CRC length */
#define ETH_MAX_PAYLOAD_LEN (1500) /* Maximum Ethernet payload size */
#define ETH_MAX_PACKET_SIZE (ETH_HEADER_LEN + ETH_VLAN_TAG_LEN + ETH_MAX_PAYLOAD_LEN + ETH_CRC_LEN) /* Maximum frame size (1522 Bytes) */

typedef enum {
    ETH_LINK_UP,  /*!< Ethernet link is up */
    ETH_LINK_DOWN /*!< Ethernet link is down */
} eth_link_t;

int emac_opencores_read_phy_reg(uint32_t phy_addr, uint32_t phy_reg, uint32_t *reg_value);
int emac_opencores_write_phy_reg(uint32_t phy_addr, uint32_t phy_reg, uint32_t reg_value);

int emac_opencores_set_promiscuous(bool enable);

int emac_opencores_transmit(uint8_t *buf, uint32_t length);

int emac_opencores_set_link(eth_link_t link);

int emac_opencores_set_addr(uint8_t *addr);
int emac_opencores_get_addr(uint8_t *addr);

int emac_opencores_stop(void);
int emac_opencores_start(void);

int emac_opencores_set_rx_data_callback(int (*callback)(void *priv, uint8_t *buf, uint32_t buf_len), void *priv);

int emac_opencores_deinit(void);
int emac_opencores_init(uint32_t rx_task_stack_size, uint32_t rx_task_prio);

#ifdef __cplusplus
}
#endif

#endif /* __EMAC_OPENCORES_H__ */
