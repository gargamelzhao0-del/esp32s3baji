#include "settings.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static TaskHandle_t reader;
static SemaphoreHandle_t finished;

static int nibble(char c)
{
    if (c >= '0' && c <= '9') return c-'0';
    if (c >= 'a' && c <= 'f') return c-'a'+10;
    if (c >= 'A' && c <= 'F') return c-'A'+10;
    return -1;
}

static bool from_hex(const char *hex, char *text, size_t capacity)
{
    size_t n = strlen(hex);
    if (n%2 || n/2 >= capacity) return false;
    for (size_t i=0; i<n; i+=2) {
        int a=nibble(hex[i]), b=nibble(hex[i+1]);
        if (a<0 || b<0 || !(a|b)) return false;
        text[i/2] = (char)(16*a+b);
    }
    text[n/2] = 0;
    return true;
}

static void reply(const char *line)
{
    usb_serial_jtag_write_bytes(line, strlen(line), pdMS_TO_TICKS(150));
}

static void handle(char *line, const badge_settings_t *s)
{
    if (!strcmp(line,"BAJI_PAIR?")) {
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
    char line[216], buf[48];
    size_t length = 0;
    bool dropping = false;
    while (!ulTaskNotifyTake(pdTRUE,0)) {
        int n = usb_serial_jtag_read_bytes(buf,sizeof(buf),pdMS_TO_TICKS(100));
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
    vTaskDelete(NULL);
}

esp_err_t badge_setup_start(const badge_settings_t *s)
{
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) return err;
#if CONFIG_VFS_SUPPORT_IO && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    /* Logs and setup responses share the same physical TX FIFO. Queue both
     * through the driver so the console cannot interleave bytes via polling. */
    usb_serial_jtag_vfs_use_driver();
#endif
    finished = xSemaphoreCreateBinary();
    if (!finished || xTaskCreate(read_setup,"badge_setup",3072,(void *)s,2,&reader) != pdPASS) {
        if (finished) vSemaphoreDelete(finished);
        finished=NULL;
#if CONFIG_VFS_SUPPORT_IO && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
        usb_serial_jtag_vfs_use_nonblocking();
#endif
        usb_serial_jtag_driver_uninstall();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void badge_setup_stop(void)
{
    if (!reader) return;
    xTaskNotifyGive(reader);
    xSemaphoreTake(finished,portMAX_DELAY);
    reader=NULL;
    vSemaphoreDelete(finished);
    finished=NULL;
#if CONFIG_VFS_SUPPORT_IO && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    usb_serial_jtag_vfs_use_nonblocking();
#endif
    usb_serial_jtag_driver_uninstall();
}
