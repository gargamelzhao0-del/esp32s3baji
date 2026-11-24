/* RAM-backed test partition. The decoder itself uses the target partition
 * path and the same streaming checksum/inflate API as the ESP32-S3. */
#include "esp_partition.h"
#include <string.h>

inflate_fn inflate_host;
checksum_fn checksum_host;
static esp_partition_t part;
static const uint8_t *storage;
void mount_bank(const void *data,size_t length,inflate_fn inflate,checksum_fn checksum)
{
    storage=data;
    part.size=length;
    inflate_host=inflate;
    checksum_host=checksum;
}

const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label)
{
    (void)type; (void)subtype;
    return storage && !strcmp(label,"hdmotion") ? &part : NULL;
}

esp_err_t esp_partition_read(const esp_partition_t *p,size_t offset,void *dest,size_t size)
{
    if (p!=&part || offset>p->size || size>p->size-offset) return ESP_ERR_INVALID_ARG;
    memcpy(dest,storage+offset,size);
    return ESP_OK;
}
