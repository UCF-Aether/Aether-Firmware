#include <stdint.h>
#include <kernel.h>
#include <device.h>
#include <lorawan/lorawan.h>
#include <logging/log.h>
#include <stdlib.h>
#include <zephyr.h>
#include <cayenne.h>
#include "lora.h"

LOG_MODULE_DECLARE(aether);

// TODO: change max packet size with lorawan_register_dr_changed_callback 

#define LORAWAN_RETRY_DELAY 5000
#define LORAWAN_DELAY       3000
#define MSGQ_GET_TIMEOUT    K_USEC(50)
#define GROUPING_TIMEOUT    K_MSEC(1)

K_TIMER_DEFINE(grouping_timer, NULL, NULL);

uint8_t dr_max_size = 11;   

struct lorawan_join_config join_cfg;


#ifdef USE_ABP

void set_join_cfg(struct lorawan_join_config *config) {
  uint32_t dev_addr = LORAWAN_DEV_ADDR;
  uint8_t dev_eui[] = LORAWAN_DEV_EUI;
  uint8_t app_eui[] = LORAWAN_APP_EUI;
  uint8_t app_skey[] = LORAWAN_APP_SKEY;
  uint8_t nwk_skey[] = LORAWAN_NWK_SKEY;

  LOG_INF("Setting activation method to ABP");
  config->mode = LORAWAN_ACT_ABP;
  config->dev_eui = dev_eui;
  config->abp.dev_addr = dev_addr;
  config->abp.app_eui = app_eui;
  config->abp.app_skey = app_skey;
  config->abp.nwk_skey = nwk_skey;
}


#else

void set_join_cfg(struct lorawan_join_config *config) {
  uint8_t dev_eui[] = LORAWAN_APP_EUI;
  uint8_t join_eui[] = LORAWAN_JOIN_EUI;
  uint8_t app_key[] = LORAWAN_APP_KEY;

  LOG_INF("Setting activation method to OTAA");
  config->mode = LORAWAN_ACT_OTAA;
  config->dev_eui = dev_eui;
  config->otaa.join_eui = join_eui;
  config->otaa.app_key = app_key;
  config->otaa.nwk_key = app_key;
}

#endif /* USE_ABP */


#ifdef LORA_REAL_DATA

int send(const struct device *lora_dev, uint8_t *buffer, int buffer_len) {
  int ret;
  LOG_INF("sending %d bytes", buffer_len);
  LOG_HEXDUMP_INF(buffer, buffer_len, "Lora send buffer");

  do {
    ret = lorawan_send(2, buffer, buffer_len, LORAWAN_MSG_CONFIRMED);
    if (ret == -EAGAIN) {
      LOG_ERR("lorawan_send failed: %d. Continuing...\n", ret);
      k_msleep(LORAWAN_RETRY_DELAY);
    }
  } while (ret == -EAGAIN);

  if (ret < 0) {
    LOG_ERR("lorawan_send failed: %d\n", ret);
    return -EINVAL;
  }

  // TODO: log dbg array
  return 0;
}

#else

int send(const struct device *lora_dev, uint8_t *buffer, int buffer_len) {
  LOG_INF("sending %d bytes", buffer_len);
  LOG_HEXDUMP_INF(buffer, buffer_len, "Lora send buffer");
  return 0;
}

#endif /* LORA_REAL_DATA */


void dl_callback(uint8_t port, bool data_pending,
      int16_t rssi, int8_t snr,
      uint8_t len, const uint8_t *data)
{
  LOG_DBG("Port %d, Pending %d, RSSI %ddB, SNR %ddBm",
      port, data_pending, rssi, snr);

  if (data) {
    LOG_HEXDUMP_DBG(data, len, "Payload: ");
  }
}


void lorwan_datarate_changed(enum lorawan_datarate dr)
{
  uint8_t unused, max_size;

  lorawan_get_payload_sizes(&unused, &max_size);

  dr_max_size = max_size;
  LOG_DBG("New Datarate: DR_%d, Max Payload %d", dr, max_size);
}

struct lorawan_downlink_cb downlink_cb = {
  .port = LW_RECV_PORT_ANY,
  .cb = dl_callback
};


// TODO: unit test
int create_packet(uint8_t *buffer, struct k_msgq *msgq, uint8_t max_packet_len) {
  uint8_t num_bytes = 0;
  struct reading reading;

  while (num_bytes < max_packet_len && k_msgq_num_used_get(msgq) > 0) {
    k_msgq_peek(msgq, (void *) &reading);

    if (num_bytes + get_reading_size(&reading) <= max_packet_len) {
      k_msgq_get(msgq, (void *) &reading, K_NO_WAIT);
      num_bytes += cayenne_packetize(buffer + num_bytes, &reading);
    }
    else {
      break;
    }
  }

  // Zero fill until 7 bytes - the min
  // for (; num_bytes < 7; num_bytes++) {
  //   buffer[num_bytes] = 0;
  // }

  return num_bytes;
}


void lora_entry_point(void *_msgq, void *arg2, void *arg3) {
  const struct device *lora_dev;
  struct k_msgq *msgq = _msgq;
  // struct lorawan_join_config join_cfg;
  int ret = 0;

  // Lowest DR 
  //TODO: change based on DR
  uint8_t buffer[256];

  lora_dev = device_get_binding(DEFAULT_RADIO);
  if (!lora_dev) {
    printk("%s Device not found\n", DEFAULT_RADIO);
    return;
  }

  ret = lorawan_start();
  if (ret < 0) {
    printk("lorawan_start failed: %d\n", ret);
    return;
  }

  lorawan_register_downlink_callback(&downlink_cb);
  lorawan_register_dr_changed_callback(lorwan_datarate_changed);
  lorawan_enable_adr(true);
  lorawan_set_conf_msg_tries(10);
  lorawan_set_datarate(LORAWAN_DR_3);

  // join_cfg.mode = LORAWAN_ACT_ABP;
  uint32_t dev_addr = LORAWAN_DEV_ADDR;
  uint8_t dev_eui[] = LORAWAN_DEV_EUI;
  uint8_t app_eui[] = LORAWAN_APP_EUI;
  uint8_t app_skey[] = LORAWAN_APP_SKEY;
  uint8_t nwk_skey[] = LORAWAN_NWK_SKEY;
  join_cfg.mode = LORAWAN_ACT_ABP;
  join_cfg.dev_eui = dev_eui;
  join_cfg.abp.dev_addr = dev_addr;
  join_cfg.abp.app_eui = app_eui;
  join_cfg.abp.app_skey = app_skey;
  join_cfg.abp.nwk_skey = nwk_skey;

  LOG_INF("Joining lorawan network");
  ret = lorawan_join(&join_cfg);
  if (ret < 0) {
    LOG_ERR("lorawan_join_network failed: %d", ret);
    return;
  }

  // Send some dummy packets

  // Main loop
  struct reading reading;
  int reading_size;
  uint8_t num_bytes = 0;
  uint8_t msgs_left = 0;
  while (1) {
    // For some reason, using K_FOREVER causes a hang, when the thread should be preemptable
    if (k_msgq_num_used_get(msgq) == 0) {
      k_yield();
      // k_msleep(500);
      continue;
    }

    num_bytes = create_packet(buffer, msgq, dr_max_size);

    send(lora_dev, buffer, num_bytes);

    LOG_INF("msgq used=%d", k_msgq_num_used_get(msgq));

    k_yield();
  }
}
