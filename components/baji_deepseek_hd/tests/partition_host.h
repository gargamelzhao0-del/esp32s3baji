#pragma once

#include "host.h"

typedef struct { size_t size; } esp_partition_t;
enum { ESP_PARTITION_TYPE_DATA = 1 };
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
esp_err_t esp_partition_read(const esp_partition_t *part, size_t offset, void *out, size_t bytes);
