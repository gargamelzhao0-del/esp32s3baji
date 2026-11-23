#include "wifi_http.h"
#include "esp_timer.h"
#include <strings.h>

enum { MAX_BODY = 192, BODY_TIMEOUT_US = 2000000 };
static const char media_type[] = "application/octet-stream";

static esp_err_t reply(httpd_req_t *req, const char *status)
{
    httpd_resp_set_status(req, status);
    return httpd_resp_send(req, NULL, 0);
}

/* HTTPD purges unread bytes after a successful handler. Closing a rejected
 * request prevents a large or stalled body from occupying the server task. */
static esp_err_t reject_body(httpd_req_t *req, const char *status)
{
    httpd_resp_set_hdr(req, "Connection", "close");
    reply(req, status);
    return ESP_FAIL;
}

static esp_err_t hello(httpd_req_t *req)
{
    if (req->content_len != 0) return reject_body(req, "400 Bad Request");
    baji_link_endpoint_t *endpoint = req->user_ctx;
    uint8_t nonce[16];
    endpoint->hello(endpoint->context, nonce);
    httpd_resp_set_type(req, media_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)nonce, sizeof(nonce));
}

static esp_err_t accept_state(httpd_req_t *req)
{
    if (req->content_len > MAX_BODY) return reject_body(req, "413 Payload Too Large");
    if (req->content_len == 0) return reply(req, "400 Bad Request");
    char type[sizeof(media_type)];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strcasecmp(type, media_type) != 0) return reject_body(req, "415 Unsupported Media Type");

    uint8_t frame[MAX_BODY];
    size_t received = 0;
    int64_t deadline = esp_timer_get_time() + BODY_TIMEOUT_US;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, (char *)frame + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT || esp_timer_get_time() >= deadline) {
            return reject_body(req, "408 Request Timeout");
        }
        if (n <= 0) return ESP_FAIL;
        received += (size_t)n;
    }

    baji_link_endpoint_t *endpoint = req->user_ctx;
    switch (endpoint->receive(endpoint->context, frame, received)) {
        case ESP_OK: return reply(req, "204 No Content");
        case ESP_ERR_INVALID_CRC: return reply(req, "401 Unauthorized");
        case ESP_ERR_INVALID_STATE: return reply(req, "409 Conflict");
        case ESP_ERR_INVALID_ARG:
        case ESP_ERR_INVALID_SIZE:
        case ESP_ERR_INVALID_VERSION:
        case ESP_ERR_INVALID_RESPONSE: return reply(req, "400 Bad Request");
        default: return reply(req, "503 Service Unavailable");
    }
}

esp_err_t baji_wifi_http_start(httpd_handle_t *server, baji_link_endpoint_t *endpoint)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* The display owner and its SPI ISR use core 1 on the wireless build. */
    config.core_id = 0;
    config.task_priority = 3;
    config.max_open_sockets = 3;
    config.max_uri_handlers = 2;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 1;
    esp_err_t err = httpd_start(server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t get = {.uri="/v1/hello", .method=HTTP_GET, .handler=hello, .user_ctx=endpoint};
    const httpd_uri_t post = {.uri="/v1/state", .method=HTTP_POST, .handler=accept_state, .user_ctx=endpoint};
    err = httpd_register_uri_handler(*server, &get);
    if (err == ESP_OK) err = httpd_register_uri_handler(*server, &post);
    return err; /* The lifecycle owner stops the server on a registration failure. */
}
