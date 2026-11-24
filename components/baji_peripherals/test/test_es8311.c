#include "peripherals_private.h"
#include <assert.h>
#include <stdio.h>

static int writes, reads, unlocks;
static uint8_t id_low=0x11, volume=0xa8;
static bool powered_up;
static uint8_t regs[256];
esp_err_t baji_peripheral_lock(baji_peripherals_t *p) {return p ? ESP_OK : ESP_ERR_INVALID_ARG;}
int xSemaphoreGive(SemaphoreHandle_t ignored) {(void)ignored; unlocks++; return pdTRUE;}
esp_err_t baji_attach_i2c(baji_peripherals_t *p,uint8_t addr,i2c_master_dev_handle_t *dev)
{assert(addr==0x18 && dev==&p->speaker); *dev=p; return ESP_OK;}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev,const uint8_t *data,size_t size,int timeout)
{
    assert(dev && size==2 && timeout>0); regs[data[0]]=data[1]; writes++;
    if (data[0]==0x31 && data[1]==0) powered_up=true;
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev,const uint8_t *tx,size_t tx_n,
                                      uint8_t *rx,size_t rx_n,int timeout)
{
    static const uint8_t readback[]={0xfd,0xfe,0x32};
    /* This codec needs explicit register selection even for consecutive IDs. */
    assert(dev && tx_n==1 && rx_n==1 && timeout>0 && powered_up);
    assert(*tx==readback[reads++%3]);
    *rx=*tx==0xfd?0x83 : *tx==0xfe?id_low:volume;
    return ESP_OK;
}
int main(void)
{
    baji_peripherals_t p={0};
    id_low=0x33;
    assert(baji_peripherals_speaker_init(&p)==ESP_ERR_INVALID_RESPONSE && !p.speaker_ready);
    id_low=0x11;volume=0xff;
    assert(baji_peripherals_speaker_init(&p)==ESP_ERR_INVALID_RESPONSE && !p.speaker_ready);
    volume=0xa8;
    assert(baji_peripherals_speaker_init(&p)==ESP_OK && p.speaker_ready);
    assert(regs[0x32]==0xa8 && regs[0x09]==0x0c && regs[0x0d]==1 && reads==9);
    int count=writes;
    assert(baji_peripherals_speaker_init(&p)==ESP_OK && writes==count && reads==9 && unlocks==4);
    puts("ES8311 single-register ID and DAC volume readback, failed-init retry and idempotency passed");
}
