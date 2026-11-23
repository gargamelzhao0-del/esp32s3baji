#include "partition_host.h"
#include <string.h>

/* The host fixture exposes a real partition capacity, with readable bytes
 * only through the encoded file's end. This detects accidental CRC of padding. */
static esp_partition_t partition;
static const uint8_t *image;
static size_t available, failure;
const uint8_t _binary_scene_hds_start[1] = {0}, _binary_scene_hds_end[1] = {0};

void test_mount(const void *data, size_t size, size_t capacity)
{
    image = data;
    available = size;
    partition.size = capacity;
    failure = SIZE_MAX;
}

void test_fail_read_at(size_t offset) { failure = offset; }

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label)
{
    if (image && type == ESP_PARTITION_TYPE_DATA && subtype == 0x40 && !strcmp(label, "hdmotion"))
        return &partition;
    return NULL;
}

esp_err_t esp_partition_read(const esp_partition_t *part, size_t offset, void *out, size_t bytes)
{
    if (part != &partition || offset > available || bytes > available - offset ||
        (offset <= failure && failure - offset < bytes))
        return ESP_ERR_INVALID_ARG;
    memcpy(out, image + offset, bytes);
    return ESP_OK;
}
