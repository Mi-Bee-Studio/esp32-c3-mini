/* app_net — 板端 Web 维护页 + WiFi 配网（前后端一体，:80）。env-csi-station 版。
 * 同构拷贝自本仓 blink 工程的 app_web 模块（web 配网/OTA 家族基线），改动点：
 *   1. 模块与入口更名 app_web→app_net，固件名 env-csi-station v0.1.0；
 *   2. AP 热点 ssid "csi-station"（原为 blink 工程热点名），页面标题 CSI STATION；
 *   3. /api/status 增补 temp/rh/csi/csi_hz 字段（extern 自 main.c 的
 *      station_env/station_csi_pkts/station_csi_hz，NaN 一律守卫为 0），
 *      状态卡同步渲染温湿度与 CSI 包率；GOT_IP 时记录 IP 字符串；
 *   4. GOT_IP→app_csi_on_wifi(true)（关联即开 CSI+网关 ping），
 *      DISCONNECTED→app_csi_on_wifi(false)（停 ping）。
 * 未动：NVS 凭据读取/擦除重试、APSTA 结构、/api/wifi /api/reboot /ota
 * 三个 handler 逐字保留（ota 流式写备用槽→校验→切槽→延迟重启）。
 * 看门狗与 OTA 无耦合：上传在 httpd 任务，主循环照常喂狗。
 * 5. 28h 浸泡修复（2026-10-22/23 serialtap 日志证据）：LAN 轮询器 keep-alive
 *    长连接占满 httpd 全部 7 槽 → accept(23)/EMFILE 风暴 2021 条、Web 挂死
 *    27h+（TCP 可建链、发请求即被 reset）。标准 esp_http_server 双对策：
 *    lru_purge_socket=true（槽满逐出最久未用）+ 全路由响应带
 *    Connection: close（强制逐请求全关，杜绝滞留 keep-alive 占槽）。
 */
#include "app_net.h"
#include "app_csi.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define APP_FW_VERSION "env-csi-station v0.1.0"
#define AP_SSID "csi-station"

static const char *TAG = "web";
static httpd_handle_t s_server;
static char s_ssid[33];
static volatile bool s_connected;
static volatile int8_t s_rssi;
static bool s_have_creds;
static char s_ip[16]; /* GOT_IP 时记录，/api/status 直接用 */

/* main.c 数据出口（屏显与 Web 状态卡同源）：最近一次成功温湿度 + CSI 包数/包率 */
extern bool station_env(float *t, float *rh);
extern uint32_t station_csi_pkts(void);
extern float station_csi_hz(void);

static const char PAGE_HTML[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>CSI STATION</title><style>"
"body{font-family:sans-serif;max-width:440px;margin:12px auto;padding:0 12px;"
"background:#111;color:#eee}h1{font-size:18px}small{color:#888}"
".card{background:#1b1b1b;border-radius:10px;padding:12px;margin:10px 0}"
".card b{display:block;margin-bottom:6px}"
"input{width:100%;box-sizing:border-box;padding:9px;margin:4px 0;border-radius:8px;"
"border:1px solid #3a3a3a;background:#242424;color:#eee;font-size:15px}"
"button{background:#2d6cdf;color:#fff;border:0;border-radius:8px;padding:9px 16px;"
"font-size:15px;margin-top:6px}#bar{height:10px;background:#333;border-radius:5px;"
"overflow:hidden;margin-top:8px}#fill{height:100%;width:0;background:#2d6cdf;"
"transition:width .2s}#msg{margin-top:8px;font-size:13px;color:#9c9}"
"</style></head><body><h1>CSI STATION <small id='fw'></small></h1>"
"<div class='card' id='st'>加载中…</div>"
"<div class='card'><b>WiFi 配网</b>"
"<input id='ssid' placeholder='SSID'><input id='pass' placeholder='密码' type='password'>"
"<button onclick='wifiSave()'>保存并连接</button></div>"
"<div class='card'><b>固件更新（OTA）</b>"
"<input type='file' id='file' accept='.bin'>"
"<button onclick='otaStart()'>上传并刷写</button>"
"<div id='bar'><div id='fill'></div></div></div>"
"<div class='card'><b>维护</b><button onclick='reboot()'>重启设备</button></div>"
"<div class='card' id='msg'></div>"
"<script>"
"const $=i=>document.getElementById(i);function msg(s){$('msg').textContent=s}"
"async function refresh(){try{const s=await(await fetch('/api/status')).json();"
"$('fw').textContent=s.fw;"
"$('st').innerHTML=(s.connected?"
"('WiFi '+s.ssid+' '+s.ip+' RSSI '+s.rssi):"
"('未联网。手机连热点 <b>" AP_SSID "</b>（密码 12345678）后访问 192.168.4.1'))"
"+'<br>temp '+s.temp+'°C · rh '+s.rh+'% · csi '+s.csi+' @'+s.csi_hz+'/s'"
"+'<br>heap '+s.heap+'K · 运行 '+s.up+'s';"
"}catch(e){$('st').textContent='状态获取失败'}}"
"async function wifiSave(){if(!$('ssid').value)return msg('SSID 不能为空');"
"const r=await fetch('/api/wifi',{method:'POST',"
"headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({ssid:$('ssid').value,pass:$('pass').value})});"
"msg(r.ok?'已保存，正在连接…查看状态卡':'保存失败：'+await r.text())}"
"function otaStart(){const f=$('file').files[0];if(!f)return msg('先选 .bin 固件文件');"
"const x=new XMLHttpRequest();x.open('POST','/ota');"
"x.upload.onprogress=e=>{$('fill').style.width=(100*e.loaded/e.total)+'%'};"
"x.onload=()=>{if(x.status==200){msg('写入成功，设备重启中…20 秒后自动刷新');"
"setTimeout(()=>location.reload(),20000)}"
"else{msg('失败 HTTP '+x.status+'：'+x.responseText)}};"
"x.onerror=()=>msg('网络错误（设备可能已在重启）');"
"msg('上传 '+f.size+' 字节…');x.send(f)}"
"async function reboot(){if(!confirm('确认重启？'))return;"
"try{await fetch('/api/reboot',{method:'POST'})}catch(e){}"
"msg('重启中…几秒后自动刷新');setTimeout(()=>location.reload(),8000)}"
"refresh();setInterval(refresh,5000);"
"</script></body></html>";

static void reboot_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static void schedule_reboot(int ms)
{
    static esp_timer_handle_t t;
    if (!t) {
        const esp_timer_create_args_t cfg = {
            .callback = reboot_cb,
            .name = "webreboot",
        };
        ESP_ERROR_CHECK(esp_timer_create(&cfg, &t));
    }
    esp_timer_stop(t);
    esp_timer_start_once(t, (uint64_t)ms * 1000);
}

static esp_err_t send_status(httpd_req_t *req, bool ok, const char *text)
{
    char out[160];
    snprintf(out, sizeof(out), "{\"status\":\"%s\",\"msg\":\"%s\"}",
             ok ? "ok" : "error", text ? text : "");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close"); /* 逐请求全关，防 keep-alive 占槽 */
    if (!ok) {
        httpd_resp_set_status(req, "400 Bad Request");
    }
    return httpd_resp_sendstr(req, out);
}

static int read_body(httpd_req_t *req, char *buf, size_t len)
{
    size_t total = 0;
    while (total < len - 1) {
        int n = httpd_req_recv(req, buf + total, len - 1 - total);
        if (n <= 0) {
            break;
        }
        total += (size_t)n;
    }
    buf[total] = 0;
    return (int)total;
}

static esp_err_t h_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_sendstr(req, PAGE_HTML);
}

static void wifi_status_json(char *out, size_t len)
{
    float t = 0.0f, rh = 0.0f;
    if (!station_env(&t, &rh)) {
        t = rh = 0.0f; /* 从未读成：按 0 上报（屏显另有 --.- 占位） */
    }
    if (isnan(t)) {
        t = 0.0f;
    }
    if (isnan(rh)) {
        rh = 0.0f;
    }
    const unsigned long csi = (unsigned long)station_csi_pkts();
    float csi_hz = station_csi_hz();
    if (isnan(csi_hz)) {
        csi_hz = 0.0f;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        s_rssi = ap.rssi;
    }
    snprintf(out, len,
             "{\"fw\":\"%s\",\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\","
             "\"rssi\":%d,\"heap\":%u,\"up\":%lu,"
             "\"temp\":%.1f,\"rh\":%.1f,\"csi\":%lu,\"csi_hz\":%.1f}",
             APP_FW_VERSION, s_connected ? "true" : "false", s_ssid, s_ip,
             s_rssi, (unsigned)esp_get_free_heap_size(),
             (unsigned long)(esp_timer_get_time() / 1000000),
             t, rh, csi, csi_hz);
}

static esp_err_t h_status(httpd_req_t *req)
{
    char json[384];
    wifi_status_json(json, sizeof(json));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_sendstr(req, json);
}

static bool wifi_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    s_have_creds = true;
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass ? pass : "", sizeof(wc.sta.password));
    if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) {
        return false;
    }
    s_connected = false;
    esp_wifi_disconnect(); /* 触发重连流程 */
    return true;
}

static esp_err_t h_wifi(httpd_req_t *req)
{
    char body[384];
    if (read_body(req, body, sizeof(body)) <= 0) {
        return send_status(req, false, "no body");
    }
    cJSON *msg = cJSON_Parse(body);
    if (!msg) {
        return send_status(req, false, "bad json");
    }
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(msg, "ssid");
    const cJSON *pass = cJSON_GetObjectItemCaseSensitive(msg, "pass");
    bool ok = cJSON_IsString(ssid) && ssid->valuestring[0] != '\0'
              && wifi_save(ssid->valuestring,
                           cJSON_IsString(pass) ? pass->valuestring : "");
    cJSON_Delete(msg);
    return send_status(req, ok, ok ? "saved" : "set failed");
}

static esp_err_t h_reboot(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    schedule_reboot(500);
    return ESP_OK;
}

static esp_err_t h_ota(httpd_req_t *req)
{
    static char buf[4096]; /* 静态收包缓冲（httpd 栈 8K） */
    /* esp_ota_begin 前置设置：函数内所有返回路径（含流式中断错误）都带 close */
    httpd_resp_set_hdr(req, "Connection", "close");
    if (req->content_len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"msg\":\"empty body\"}");
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"msg\":\"no ota partition\"}");
    }
    esp_ota_handle_t ota;
    if (esp_ota_begin(part, (size_t)req->content_len, &ota) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"msg\":\"ota begin failed\"}");
    }
    ESP_LOGI(TAG, "OTA 开始：目标槽 %s，%u 字节", part->label,
             (unsigned)req->content_len);
    size_t remain = (size_t)req->content_len;
    while (remain > 0) {
        int got = httpd_req_recv(req, buf, remain > sizeof(buf) ? sizeof(buf) : remain);
        if (got <= 0) {
            esp_ota_abort(ota);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_sendstr(req, "{\"status\":\"error\",\"msg\":\"recv failed\"}");
        }
        if (esp_ota_write(ota, buf, (size_t)got) != ESP_OK) {
            esp_ota_abort(ota);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_sendstr(req, "{\"status\":\"error\",\"msg\":\"ota write failed\"}");
        }
        remain -= (size_t)got;
    }
    if (esp_ota_end(ota) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"msg\":\"image invalid\"}");
    }
    ESP_LOGI(TAG, "OTA 写入并通过校验，1s 后重启进新固件");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"msg\":\"done, rebooting\"}");
    schedule_reboot(1000);
    return ESP_OK;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_have_creds) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        app_csi_on_wifi(false); /* 停网关 ping（无关联即无 CSI 包） */
        if (!s_have_creds) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(s_ssid[0] && s_connected ? 5000 : 1000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        s_connected = true;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "WiFi 已连接：%s ip=" IPSTR, s_ssid, IP2STR(&e->ip_info.ip));
        app_csi_on_wifi(true); /* 关联成功：使能 CSI + 网关 ping 激励 */
    }
}

void app_net_init(void)
{
    esp_err_t nerr = nvs_flash_init();
    if (nerr == ESP_ERR_NVS_NO_FREE_PAGES || nerr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nerr = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nerr);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    wifi_config_t ap = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = sizeof(AP_SSID) - 1,
            .password = "12345678",
            .channel = 1,
            .max_connection = 2,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) == ESP_OK) {
        char pass[65] = "";
        size_t sl = sizeof(s_ssid), pl = sizeof(pass);
        if (nvs_get_str(h, "ssid", s_ssid, &sl) == ESP_OK
            && nvs_get_str(h, "pass", pass, &pl) == ESP_OK && s_ssid[0]) {
            s_have_creds = true;
            wifi_config_t wc = { 0 };
            strlcpy((char *)wc.sta.ssid, s_ssid, sizeof(wc.sta.ssid));
            strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
            ESP_LOGI(TAG, "NVS 有凭据，关联 \"%s\"…", s_ssid);
        }
        nvs_close(h);
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 8;
    cfg.max_open_sockets = 7;    /* 显式声明槽位数（默认即 7），与 lru 联动 */
    cfg.lru_purge_enable = true; /* 7 槽全忙时逐出最久未用连接（accept(23) 风暴对策；IDF v6.0 字段名） */
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd 启动失败");
        return;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = h_root },
        { .uri = "/api/status", .method = HTTP_GET, .handler = h_status },
        { .uri = "/api/wifi", .method = HTTP_POST, .handler = h_wifi },
        { .uri = "/api/reboot", .method = HTTP_POST, .handler = h_reboot },
        { .uri = "/ota", .method = HTTP_POST, .handler = h_ota },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }
    ESP_LOGI(TAG, "维护页 :80 就绪（在网=STA IP；未配网=热点 " AP_SSID "/12345678 → 192.168.4.1）");
}
