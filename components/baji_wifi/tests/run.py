"""Run the production HTTP adapter against a deterministic in-process HTTPD double."""
import os
import subprocess
import tempfile
from pathlib import Path


HERE = Path(__file__).resolve().parent
COMPONENT = HERE.parent


with tempfile.TemporaryDirectory() as temp:
    headers = Path(temp)
    (headers / "esp_err.h").write_text("""#pragma once
typedef int esp_err_t;
enum { ESP_OK, ESP_ERR_INVALID_CRC, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG,
       ESP_ERR_INVALID_SIZE, ESP_ERR_INVALID_VERSION, ESP_ERR_INVALID_RESPONSE };
#define ESP_FAIL (-1)
""")
    (headers / "esp_http_server.h").write_text("""#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
typedef void *httpd_handle_t;
typedef struct { size_t content_len; void *user_ctx; } httpd_req_t;
typedef struct { const char *uri; int method; int (*handler)(httpd_req_t *); void *user_ctx; } httpd_uri_t;
typedef struct { int core_id,task_priority,max_open_sockets,max_uri_handlers,lru_purge_enable;
                 int recv_wait_timeout,send_wait_timeout; } httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
enum {HTTP_GET,HTTP_POST,HTTPD_SOCK_ERR_TIMEOUT=-2};
int httpd_start(httpd_handle_t *,const httpd_config_t *);
int httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *);
int httpd_resp_set_type(httpd_req_t *,const char *);
int httpd_resp_set_status(httpd_req_t *,const char *);
int httpd_resp_set_hdr(httpd_req_t *,const char *,const char *);
int httpd_resp_send(httpd_req_t *,const char *,int);
int httpd_req_get_hdr_value_str(httpd_req_t *,const char *,char *,size_t);
int httpd_req_recv(httpd_req_t *,char *,size_t);
""")
    (headers / "esp_timer.h").write_text("#include <stdint.h>\nint64_t esp_timer_get_time(void);\n")
    exe = headers / ("wifi_http_test.exe" if os.name == "nt" else "wifi_http_test")
    subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(headers), "-I", str(COMPONENT), "-I", str(COMPONENT.parent / "baji_link/include"),
                    str(COMPONENT / "wifi_http.c"), str(HERE / "http_test.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
