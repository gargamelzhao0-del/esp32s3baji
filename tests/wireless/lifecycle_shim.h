#pragma once
#include "sdk_shim.h"
#include <stdbool.h>

#define CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED 1
#define CONFIG_VFS_SUPPORT_IO 1
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define NVS_READWRITE 1
#define ESP_MAC_WIFI_SOFTAP 1
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
typedef int nvs_handle_t;
typedef void *TaskHandle_t;
typedef struct { int tx_buffer_size, rx_buffer_size; } usb_serial_jtag_driver_config_t;
#define USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT() ((usb_serial_jtag_driver_config_t){256,256})

esp_err_t nvs_flash_init(void);
esp_err_t nvs_open(const char *,int,nvs_handle_t *);
esp_err_t nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);
esp_err_t nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
esp_err_t esp_read_mac(uint8_t *,int);
void bootloader_random_enable(void);
void bootloader_random_disable(void);
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_loop_delete_default(void);
static inline const char *esp_err_to_name(int err) { (void)err; return "test"; }
static inline void discard_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
#define ESP_LOGW(...) discard_log(__VA_ARGS__)
int xTaskCreate(void (*entry)(void *),const char *,unsigned,void *,int,TaskHandle_t *);
typedef int BaseType_t;
enum { eSetBits = 1 };
typedef enum { USJ_SELECT_READ_NOTIF, USJ_SELECT_WRITE_NOTIF } usj_select_notif_t;
typedef void (*usj_select_notif_callback_t)(usj_select_notif_t, BaseType_t *);
void usb_serial_jtag_set_select_notif_callback(usj_select_notif_callback_t);
void xTaskNotify(TaskHandle_t, uint32_t, int);
void xTaskNotifyFromISR(TaskHandle_t, uint32_t, int, BaseType_t *);
void xTaskNotifyWait(uint32_t, uint32_t, uint32_t *, unsigned);
void vTaskSuspend(void *);
void vTaskDelete(void *);
int usb_serial_jtag_read_bytes(void *,uint32_t,unsigned);
int usb_serial_jtag_write_bytes(const void *,size_t,unsigned);
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *);
esp_err_t usb_serial_jtag_driver_uninstall(void);
void usb_serial_jtag_vfs_use_driver(void);
void usb_serial_jtag_vfs_use_nonblocking(void);
