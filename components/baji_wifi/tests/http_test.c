/* Exercise the real URI handlers against short socket reads and rejected
 * protocol packets. The host double records exactly what HTTPD would send. */
#include "wifi_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static httpd_uri_t handlers[2];
static unsigned registered, calls, chunk;
static size_t offset;
static uint8_t input[256], output[32];
static const char *status, *content_type;
static size_t output_len;
static int result;
static int64_t now, elapsed_per_read;
static bool closed;

int httpd_start(httpd_handle_t *s, const httpd_config_t *cfg)
{
    assert(cfg->max_uri_handlers == 2 && cfg->recv_wait_timeout && cfg->send_wait_timeout);
    *s = handlers;
    return ESP_OK;
}
int httpd_register_uri_handler(httpd_handle_t s, const httpd_uri_t *u)
{
    assert(s == handlers && registered < 2);
    handlers[registered++] = *u;
    return ESP_OK;
}
int httpd_resp_set_type(httpd_req_t *r, const char *t)
{ (void)r; assert(strcmp(t,"application/octet-stream") == 0); return 0; }
int httpd_resp_set_status(httpd_req_t *r, const char *s) { (void)r; status=s; return 0; }
int httpd_resp_set_hdr(httpd_req_t *r, const char *k, const char *v)
{ (void)r; if (!strcmp(k,"Connection")) closed = !strcmp(v,"close"); return 0; }
int httpd_resp_send(httpd_req_t *r, const char *s, int n)
{
    (void)r;
    assert(n >= 0 && (size_t)n <= sizeof(output));
    output_len = (size_t)n;
    if (n) memcpy(output,s,output_len);
    return 0;
}
int httpd_req_get_hdr_value_str(httpd_req_t *r, const char *name, char *dst, size_t cap)
{
    (void)r;
    assert(!strcmp(name,"Content-Type"));
    if (!content_type || strlen(content_type) >= cap) return ESP_FAIL;
    strcpy(dst,content_type);
    return ESP_OK;
}
int64_t esp_timer_get_time(void) { return now; }
int httpd_req_recv(httpd_req_t *r, char *buf, size_t n)
{
    (void)r;
    if (!chunk) return HTTPD_SOCK_ERR_TIMEOUT;
    if (n > chunk) n = chunk;
    assert(offset+n <= sizeof(input));
    memcpy(buf, input+offset, n);
    offset += n;
    now += elapsed_per_read;
    return (int)n;
}
static void nonce(void *context, uint8_t out[16]) { assert(context == input); memcpy(out,input,16); }
static int receive(void *context, const uint8_t *p, size_t n)
{
    assert(context == input && n == offset && !memcmp(p, input, n));
    calls++;
    return result;
}
static void run(httpd_req_t *r, const char *expected, unsigned wanted_calls, bool close)
{
    calls=offset=0; output_len=0; status="200 OK"; closed=false;
    int err = handlers[1].handler(r);
    assert(!strcmp(status,expected) && calls==wanted_calls && closed==close &&
           err==(close ? ESP_FAIL : ESP_OK));
}
int main(void)
{
    for (size_t i=0; i<sizeof(input); i++) input[i]=(uint8_t)i;
    baji_link_endpoint_t ep={.context=input,.hello=nonce,.receive=receive};
    httpd_handle_t s=NULL;
    assert(baji_wifi_http_start(&s,&ep)==ESP_OK && registered==2);
    assert(!strcmp(handlers[0].uri,"/v1/hello") && handlers[0].method==HTTP_GET &&
           !strcmp(handlers[1].uri,"/v1/state") && handlers[1].method==HTTP_POST);
    httpd_req_t req={.user_ctx=handlers[0].user_ctx};
    assert(handlers[0].handler(&req)==ESP_OK && output_len==16 && !memcmp(output,input,16));
    req.user_ctx=handlers[1].user_ctx;
    req.content_len=192; content_type="Application/Octet-Stream"; chunk=7;
    run(&req,"204 No Content",1,false);
    result=ESP_ERR_INVALID_CRC; run(&req,"401 Unauthorized",1,false);
    result=ESP_ERR_INVALID_STATE; run(&req,"409 Conflict",1,false);
    result=ESP_ERR_INVALID_SIZE; run(&req,"400 Bad Request",1,false);
    req.content_len=193; run(&req,"413 Payload Too Large",0,true);
    req.content_len=0; run(&req,"400 Bad Request",0,false);
    req.content_len=128; content_type="text/plain"; run(&req,"415 Unsupported Media Type",0,true);
    content_type="application/octet-stream"; chunk=0; run(&req,"408 Request Timeout",0,true);
    chunk=1; elapsed_per_read=800000; run(&req,"408 Request Timeout",0,true);
    assert(offset==3);
    puts("Wi-Fi HTTP hello, bounded fragmented reads, timeout, media type and status mapping passed");
}
