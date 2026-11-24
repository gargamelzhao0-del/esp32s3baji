#include "settings.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_select.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static TaskHandle_t reader;
static SemaphoreHandle_t finished;
static baji_link_endpoint_t endpoint;
enum { USB_PACKET_MAX = 192, COOKIE_LEN = 16, USB_LINE_BYTES = 10 + COOKIE_LEN + 1 + 2 * USB_PACKET_MAX + 1 };
enum { USB_RX = 1, USB_STOP = 2 };

/* Setup owns the USB RX stream. Console logging uses only the TX driver. */
static void usb_event(usj_select_notif_t event, BaseType_t *woken)
{
    if (event == USJ_SELECT_READ_NOTIF) xTaskNotifyFromISR(reader, USB_RX, eSetBits, woken);
}

static int nibble(char c)
{
    if (c >= '0' && c <= '9') return c-'0';
    if (c >= 'a' && c <= 'f') return c-'a'+10;
    if (c >= 'A' && c <= 'F') return c-'A'+10;
    return -1;
}

static int decode_hex(const char *hex, uint8_t *bytes, size_t capacity)
{
    size_t n = strlen(hex);
    if (n%2 || n/2 > capacity) return -1;
    for (size_t i=0; i<n; i+=2) {
        int a=nibble(hex[i]), b=nibble(hex[i+1]);
        if (a<0 || b<0) return -1;
        bytes[i/2] = (uint8_t)(16*a+b);
    }
    return (int)(n/2);
}

static bool from_hex(const char *hex, char *text, size_t capacity)
{
    int n = decode_hex(hex, (uint8_t *)text, capacity-1);
    if (n < 0 || memchr(text, 0, n)) return false;
    text[n] = 0;
    return true;
}

static void reply(const char *line)
{
    usb_serial_jtag_write_bytes(line, strlen(line), pdMS_TO_TICKS(150));
}

static void handle(char *line, const badge_settings_t *s)
{
    if (!strncmp(line, "BAJI_LINK ", 10)) {
        char *cookie = line+10, *payload = strchr(cookie, ' ');
        if (!payload || payload-cookie != COOKIE_LEN) return;
        *payload++ = 0;
        for (size_t i = 0; i < COOKIE_LEN; i++) if (nibble(cookie[i]) < 0) return;
        char nonce_hex[33], response[72];
        const char *status = "INVALID";
        if (!strcmp(payload, "HELLO")) {
            uint8_t nonce[16];
            endpoint.hello(endpoint.context, nonce);
            for (size_t i = 0; i < sizeof(nonce); i++) snprintf(nonce_hex+2*i, 3, "%02x", nonce[i]);
            status = nonce_hex;
        } else {
            uint8_t packet[USB_PACKET_MAX];
            int length = decode_hex(payload, packet, sizeof(packet));
            if (length > 0) {
                switch (endpoint.receive(endpoint.context, packet, length)) {
                    case ESP_OK: status = "OK"; break;
                    case ESP_ERR_INVALID_CRC: status = "AUTH"; break;
                    case ESP_ERR_INVALID_STATE: status = "BUSY"; break;
                    case ESP_ERR_INVALID_SIZE: case ESP_ERR_INVALID_ARG: break;
                    default: status = "FAIL"; break;
                }
            }
        }
        snprintf(response, sizeof(response), "BAJI_LINK1 %.*s %s\n", COOKIE_LEN, cookie, status);
        reply(response);
    } else if (!strcmp(line,"BAJI_PAIR?")) {
        char hex[65], response[156];
        for (unsigned i=0; i<32; i++) snprintf(hex+i*2,3,"%02x",s->key[i]);
        snprintf(response,sizeof(response),"BAJI_PAIR1 %s %s %s\n",hex,s->ap_ssid,s->ap_pass);
        reply(response);
    } else if (!strncmp(line,"BAJI_WIFI ",10)) {
        char ssid[33], password[65];
        char *sep = strchr(line+10,':');
        if (sep) *sep++ = 0;
        bool ok = sep && from_hex(line+10,ssid,sizeof(ssid)) && from_hex(sep,password,sizeof(password))
                  && badge_settings_wifi(ssid,password) == ESP_OK;
        reply(ok ? "BAJI_WIFI_SAVED reboot\n" : "BAJI_WIFI_INVALID\n");
    }
}

static void read_setup(void *arg)
{
    char line[USB_LINE_BYTES], buf[48];
    size_t length = 0;
    bool dropping = false;
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), 0);
        uint32_t events = 0;
        xTaskNotifyWait(0, UINT32_MAX, &events, n > 0 ? 0 : portMAX_DELAY);
        if (events & USB_STOP) break;
        for (int i=0; i<n; i++) {
            if (buf[i]=='\n') {
                if (!dropping) { line[length]=0; handle(line,arg); }
                length=0; dropping=false;
            } else if (buf[i]!='\r' && !dropping) {
                if (!buf[i] || length+1>=sizeof(line)) dropping=true;
                else line[length++]=buf[i];
            }
        }
    }
    xSemaphoreGive(finished);
    /* Keep the notification target valid through ISR removal on any core. */
    vTaskSuspend(NULL);
}

esp_err_t badge_setup_start(const badge_settings_t *s, const baji_link_endpoint_t *link)
{
    endpoint = *link;
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    /* IDF discards an RX FIFO transfer if its ring is full. A complete signed
     * line can arrive in one burst before this low-priority reader runs. */
    config.rx_buffer_size = (USB_LINE_BYTES + 64) & ~63;
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) return err;
#if CONFIG_VFS_SUPPORT_IO && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    /* Logs and setup responses share the same physical TX FIFO. Queue both
     * through the driver so the console cannot interleave bytes via polling. */
    usb_serial_jtag_vfs_use_driver();
#endif
    finished = xSemaphoreCreateBinary();
    if (!finished || xTaskCreate(read_setup,"badge_setup",4096,(void *)s,2,&reader) != pdPASS) {
        if (finished) vSemaphoreDelete(finished);
        finished=NULL;
#if CONFIG_VFS_SUPPORT_IO && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
        usb_serial_jtag_vfs_use_nonblocking();
#endif
        usb_serial_jtag_driver_uninstall();
        return ESP_ERR_NO_MEM;
    }
    usb_serial_jtag_set_select_notif_callback(usb_event);
    /* A packet can already be buffered from before the ISR hook was installed. */
    xTaskNotify(reader, USB_RX, eSetBits);
    return ESP_OK;
}

void badge_setup_stop(void)
{
    if (!reader) return;
    usb_serial_jtag_set_select_notif_callback(NULL);
    xTaskNotify(reader, USB_STOP, eSetBits);
    xSemaphoreTake(finished,portMAX_DELAY);
#if CONFIG_VFS_SUPPORT_IO && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    usb_serial_jtag_vfs_use_nonblocking();
#endif
    usb_serial_jtag_driver_uninstall();
    vTaskDelete(reader);
    reader = NULL;
    vSemaphoreDelete(finished);
    finished = NULL;
}
