#pragma once

#include "baji_link_endpoint.h"
#include "esp_http_server.h"

esp_err_t baji_wifi_http_start(httpd_handle_t *server, baji_link_endpoint_t *endpoint);
