/**
 * web_server.c - 统一 Web 服务器实现
 *
 * 提供两种模式的 HTTP 服务:
 * 1. AP 模式: WiFi 配置门户 (扫描、选择、保存凭证)
 * 2. STA 模式: 配置管理仪表板 (设备信息、配置编辑、探测状态)
 *
 * 路由说明:
 * AP 模式:
 *   GET  /      → 暗色主题 WiFi 配置页面
 *   GET  /scan  → WiFi 扫描结果 JSON
 *   POST /save  → 保存 WiFi 凭证到 NVS
 *   POST /ota   → 固件上传（OTA 双槽，救援通道）
 *
 * STA 模式:
 *   GET  /            → 配置管理仪表板
 *   GET  /api/status  → 设备状态 JSON
 *   POST /api/config  → 更新配置 JSON
 *   POST /api/reload  → 热加载配置
 *   POST /ota         → 固件上传（流式写 OTA 槽→校验→切槽→延迟重启）
 *   POST /api/reboot  → 重启设备
 */

#include "web_server.h"
#include "config_manager.h"
#include "probe_manager.h"
#include "wifi_manager.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "lwip/netdb.h"
#include "cJSON.h"

static const char *TAG_AP  = "CFG_SRV";
static const char *TAG_STA = "WEB_SRV";

/* ---- AP 模式服务器 ---- */
static httpd_handle_t s_ap_server = NULL;

/* ---- STA 模式服务器 ---- */
static httpd_handle_t s_sta_server = NULL;

/* ================================================================ */
/*  AP 模式: WiFi 配置门户 (沿用 wifi_config_server.c 的 HTML)      */
/* ================================================================ */

static const char CONFIG_HTML[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP32 Blackbox</title><style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#eee;"
"min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}"
".c{background:#16213e;border-radius:12px;padding:28px;width:100%;max-width:380px}"
"h1{text-align:center;font-size:1.3em;color:#e94560;margin-bottom:4px}"
"p.sub{text-align:center;color:#8892b0;font-size:.85em;margin-bottom:18px}"
"label{display:block;margin:14px 0 4px;font-size:.82em;color:#8892b0;font-weight:600}"
"input{width:100%;padding:11px;border:1px solid #233554;border-radius:8px;"
"background:#0f3460;color:#eee;font-size:15px;outline:none}"
"input:focus{border-color:#e94560}"
".btn{width:100%;padding:13px;border:none;border-radius:8px;font-size:15px;"
"cursor:pointer;font-weight:600;transition:background .2s}"
".bp{background:#e94560;color:#fff;margin-top:18px}"
".bp:hover{background:#c73650}"
".bs{background:#233554;color:#8892b0;margin-top:6px;font-size:13px;padding:10px}"
".bs:disabled{opacity:.5;cursor:not-allowed}"
".msg{padding:10px;border-radius:8px;margin-top:14px;font-size:.88em;display:none}"
".ok{background:#0f9b58;color:#fff;display:block}"
".er{background:#e94560;color:#fff;display:block}"
"#nets{max-height:180px;overflow-y:auto;margin-top:6px}"
".ni{padding:9px 11px;background:#0f3460;border-radius:6px;margin-bottom:3px;"
"cursor:pointer;display:flex;justify-content:space-between;font-size:.88em}"
".ni:hover{background:#1a4a8a}"
".ni.sel{background:#e94560}"
".ni .r{font-size:.72em;color:#8892b0}"
".ni.sel .r{color:rgba(255,255,255,.7)}"
"</style></head><body><div class='c'>"
"<h1>ESP32 Blackbox</h1>"
"<p class='sub'>WiFi Configuration</p>"
"<div id='m' class='msg'></div>"
"<button class='btn bs' id='sb' onclick='scan()'>Scan Networks</button>"
"<div id='nets'></div>"
"<form id='f' onsubmit='save(event)'>"
"<label>SSID</label>"
"<input type='text' id='s' required placeholder='WiFi name'>"
"<label>Password</label>"
"<input type='password' id='p' placeholder='WiFi password'>"
"<button class='btn bp' type='submit'>Save &amp; Restart</button>"
"</form>"
"<label>Firmware (.bin)</label>"
"<input type='file' id='fw' accept='.bin'>"
"<button class='btn bs' type='button' onclick='ota()'>Upload &amp; Flash</button>"
"<div style='height:8px;background:#233554;border-radius:4px;margin-top:8px;overflow:hidden'>"
"<div id='bar' style='height:100%;width:0;background:#0f9b58'></div></div>"
"</div>"
"<script>"
"function scan(){var b=document.getElementById('sb'),n=document.getElementById('nets');"
"b.disabled=true;b.textContent='Scanning...';n.innerHTML='';"
"var x=new XMLHttpRequest();x.onload=function(){b.disabled=false;"
"b.textContent='Scan Networks';if(x.status!==200){msg('Scan failed','er');return}"
"var a=JSON.parse(x.responseText);a.sort(function(a,b){return b.rssi-a.rssi});"
"n.innerHTML='';a.forEach(function(ap){if(!ap.ssid)return;"
"var d=document.createElement('div');d.className='ni';"
"d.innerHTML='<span>'+esc(ap.ssid)+'</span><span class=\"r\"'"
"+ap.rssi+'dBm'+(ap.auth>0?' 🔒':'')+'</span>';"
"d.onclick=function(){document.getElementById('s').value=ap.ssid;"
"document.querySelectorAll('.ni').forEach(function(e){e.classList.remove('sel')});"
"d.classList.add('sel')};n.appendChild(d)});"
"if(!a.length)msg('No networks found','er')};"
"x.onerror=function(){b.disabled=false;b.textContent='Scan Networks';msg('Scan failed','er')};"
"x.open('GET','/scan');x.send()}"
"function save(e){e.preventDefault();var s=document.getElementById('s').value,"
"p=document.getElementById('p').value;if(!s){msg('Enter SSID','er');return}"
"var x=new XMLHttpRequest();x.onload=function(){if(x.status===200)"
"{msg('Saved! Restarting...','ok');document.getElementById('f').style.display='none'"
";setTimeout(function(){location.reload()},5000)}else msg('Save failed','er')};"
"x.onerror=function(){msg('Network error','er')};"
"x.open('POST','/save',true);x.setRequestHeader('Content-Type','application/json');"
"x.send(JSON.stringify({ssid:s,password:p}))}"
"function msg(t,c){var m=document.getElementById('m');m.textContent=t;m.className='msg '+c}"
"function esc(s){var d=document.createElement('div');d.textContent=s;return d.innerHTML}"
"function ota(){var f=document.getElementById('fw').files[0];if(!f){msg('Select .bin','er');return}"
"var x=new XMLHttpRequest();x.open('POST','/ota');"
"x.upload.onprogress=function(e){document.getElementById('bar').style.width=(100*e.loaded/e.total)+'%'};"
"x.onload=function(){if(x.status===200){msg('OK! Rebooting...','ok');"
"setTimeout(function(){location.reload()},20000)}else msg('Flash failed','er')};"
"x.onerror=function(){msg('Network error','er')};x.send(f)}"
"</script></body></html>";

/* ---- AP: GET / ---- */
static esp_err_t ap_root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, CONFIG_HTML, -1);
}

/* ---- AP: GET /scan ---- */
static esp_err_t ap_scan_handler(httpd_req_t *req)
{
    wifi_scan_config_t scan_cfg = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .show_hidden = false,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_AP, "scan failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_FAIL;
    }

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);

    wifi_ap_record_t *records = calloc(num, sizeof(wifi_ap_record_t));
    if (!records) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    esp_wifi_scan_get_ap_records(&num, records);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < num; i++) {
        if (records[i].ssid[0] == '\0') continue;
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (char *)records[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(ap, "auth", records[i].authmode);
        cJSON_AddItemToArray(root, ap);
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    free(records);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ---- AP: POST /save ---- */
static esp_err_t ap_save_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *j_ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *j_pass = cJSON_GetObjectItem(root, "password");

    if (!j_ssid || !cJSON_IsString(j_ssid) || j_ssid->valuestring[0] == '\0') {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    const char *ssid = j_ssid->valuestring;
    const char *pass = (j_pass && cJSON_IsString(j_pass)) ? j_pass->valuestring : "";

    esp_err_t err = config_manager_wifi_save(ssid, pass);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");
        return ESP_FAIL;
    }

    const char resp[] = "{\"status\":\"ok\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK;
}

/* ================================================================ */
/*  STA 模式: 配置管理仪表板                                        */
/* ================================================================ */

static const char DASHBOARD_HTML[] =
"<!DOCTYPE html><html lang='zh-CN'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP32 Blackbox</title><style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#eee;"
"min-height:100vh;padding:16px}"
".hd{text-align:center;padding:20px 0 12px}"
".hd h1{font-size:1.4em;color:#e94560;margin-bottom:2px}"
".hd p{color:#8892b0;font-size:.85em}"
".grid{display:grid;grid-template-columns:1fr;gap:14px;max-width:720px;margin:0 auto}"
"@media(min-width:600px){.grid{grid-template-columns:1fr 1fr}}"
".cd{background:#16213e;border-radius:10px;padding:18px}"
".cd h2{font-size:1em;color:#e94560;margin-bottom:10px;border-bottom:1px solid #233554;"
"padding-bottom:6px}"
".row{display:flex;justify-content:space-between;padding:5px 0;"
"font-size:.88em;border-bottom:1px solid #0f3460}"
".row:last-child{border:none}"
".row .l{color:#8892b0}"
".row .v{color:#eee;font-weight:600}"
".full{grid-column:1/-1}"
"table{width:100%;border-collapse:collapse;font-size:.82em}"
"th{text-align:left;color:#8892b0;padding:6px 8px;border-bottom:1px solid #233554}"
"td{padding:6px 8px;border-bottom:1px solid #0f3460}"
".ok{color:#0f9b58}.fl{color:#e94560}"
"textarea{width:100%;min-height:260px;background:#0f3460;color:#eee;border:1px solid #233554;"
"border-radius:8px;padding:12px;font-family:monospace;font-size:.82em;resize:vertical;"
"outline:none}"
"textarea:focus{border-color:#e94560}"
".btns{display:flex;gap:8px;margin-top:10px;flex-wrap:wrap}"
".btn{padding:10px 20px;border:none;border-radius:8px;font-size:.9em;"
"cursor:pointer;font-weight:600;transition:background .2s}"
".bp{background:#e94560;color:#fff}"
".bp:hover{background:#c73650}"
".bs{background:#233554;color:#8892b0}"
".bs:hover{background:#2a4070}"
".bg{background:#0f9b58;color:#fff}"
".flash{position:fixed;top:16px;left:50%;transform:translateX(-50%);padding:12px 24px;"
"border-radius:8px;font-weight:600;font-size:.9em;z-index:99;transition:opacity .3s;"
"opacity:0;pointer-events:none}"
".flash.show{opacity:1}"
".fos{background:#0f9b58;color:#fff}"
".fer{background:#e94560;color:#fff}"
"</style></head><body>"
"<div class='hd'><h1>ESP32 Blackbox</h1>"
"<p id='sub'>Configuration Dashboard</p></div>"
"<div class='grid'>"
"<div class='cd'><h2>Device Info</h2>"
"<div class='row'><span class='l'>IP Address</span><span class='v' id='ip'>-</span></div>"
"<div class='row'><span class='l'>WiFi SSID</span><span class='v' id='ssid'>-</span></div>"
"<div class='row'><span class='l'>Firmware</span><span class='v' id='fwv'>-</span></div>"
"<div class='row'><span class='l'>Uptime</span><span class='v' id='up'>-</span></div>"
"</div>"
"<div class='cd'><h2>Config Summary</h2>"
"<div class='row'><span class='l'>Modules</span><span class='v' id='mc'>-</span></div>"
"<div class='row'><span class='l'>Targets</span><span class='v' id='tc'>-</span></div>"
"<div class='row'><span class='l'>Scrape Interval</span><span class='v' id='si'>-</span></div>"
"<div class='row'><span class='l'>Metrics Port</span><span class='v' id='mp'>-</span></div>"
"</div>"
"<div class='cd full'><h2>Probe Status</h2>"
"<table><thead><tr><th>Name</th><th>Target</th><th>Port</th><th>Status</th>"
"<th>Duration</th><th>Error</th></tr></thead>"
"<tbody id='ptb'></tbody></table></div>"
"<div class='cd full'><h2>Config JSON</h2>"
"<textarea id='je' spellcheck='false'></textarea>"
"<div class='btns'>"
"<button class='btn bp' onclick='saveCfg()'>Save</button>"
"<button class='btn bs' onclick='loadCfg()'>Reload</button>"
"<button class='btn bg' onclick='hotReload()'>Hot Reload</button>"
"</div></div>"
"<div class='cd full'><h2>Firmware Update (OTA)</h2>"
"<input type='file' id='fw' accept='.bin' "
"style='width:100%;padding:8px;background:#0f3460;border:1px solid #233554;border-radius:8px'>"
"<div class='btns'>"
"<button class='btn bp' onclick='otaGo()'>Upload &amp; Flash</button>"
"<button class='btn bs' onclick='doReboot()'>Reboot</button>"
"</div>"
"<div style='height:10px;background:#0f3460;border-radius:5px;margin-top:10px;overflow:hidden'>"
"<div id='bar' style='height:100%;width:0;background:#0f9b58;transition:width .2s'></div></div>"
"</div></div>"
"<div class='flash' id='fl'></div>"
"<script>"
"function $(i){return document.getElementById(i)}"
"function fl(t,s){var e=$('fl');e.textContent=t;e.className='flash '+(s||'fos')+' show';"
"setTimeout(function(){e.className='flash'},2500)}"
"function fmt(ms){if(ms<60000)return Math.floor(ms/1000)+'s';"
"var m=Math.floor(ms/60000);if(m<60)return m+'m';"
"var h=Math.floor(m/60);return h+'h '+Math.floor(m%60)+'m'}"
"function loadStatus(){var x=new XMLHttpRequest();"
"x.onload=function(){if(x.status!==200)return;"
"var d=JSON.parse(x.responseText);"
"$('fwv').textContent=d.fw;"
"$('up').textContent=fmt(d.uptime_s*1000);"
"$('mc').textContent=d.modules;$('tc').textContent=d.targets;"
"$('si').textContent=d.scrape_interval_s+'s';"
"$('mp').textContent=d.metrics_port;"
"var tb=$('ptb');tb.innerHTML='';"
"d.results.forEach(function(r){var tr=document.createElement('tr');"
"tr.innerHTML='<td>'+esc(r.name)+'</td><td>'+esc(r.target)+'</td>'"
"+'<td>'+r.port+'</td>'"
"+'<td class=\"'+(r.success?'ok':'fl')+'\">'+(r.success?'OK':'FAIL')+'</td>'"
"+'<td>'+(r.duration_ms>0?r.duration_ms+'ms':'-')+'</td>'"
"+'<td>'+(r.error||'')+'</td>';tb.appendChild(tr)})};"
"x.open('GET','/api/status');x.send()}"
"function loadCfg(){var x=new XMLHttpRequest();"
"x.onload=function(){if(x.status!==200){fl('Load failed','fer');return}"
"$('je').value=x.responseText;fl('Reloaded from SPIFFS')};"
"x.onerror=function(){fl('Load failed','fer')};"
"x.open('GET','/api/config');x.send()}"
"function saveCfg(){var j=$('je').value;if(!j.trim()){fl('Empty config','fer');return}"
"var x=new XMLHttpRequest();"
"x.onload=function(){if(x.status===200)fl('Saved');else fl('Save failed','fer')};"
"x.onerror=function(){fl('Network error','fer')};"
"x.open('POST','/api/config',true);"
"x.setRequestHeader('Content-Type','application/json');"
"x.send(j)}"
"function hotReload(){var x=new XMLHttpRequest();"
"x.onload=function(){if(x.status===200){fl('Hot reload OK');setTimeout(loadStatus,500)}"
"else fl('Reload failed','fer')};"
"x.onerror=function(){fl('Network error','fer')};"
"x.open('POST','/api/reload');x.send()}"
"function esc(s){var d=document.createElement('div');d.textContent=s;return d.innerHTML}"
"function otaGo(){var f=$('fw').files[0];if(!f){fl('Select .bin file','fer');return}"
"var x=new XMLHttpRequest();x.open('POST','/ota');"
"x.upload.onprogress=function(e){$('bar').style.width=(100*e.loaded/e.total)+'%'};"
"x.onload=function(){if(x.status===200){fl('OK! Rebooting...');"
"setTimeout(function(){location.reload()},20000)}else fl('Flash failed','fer')};"
"x.onerror=function(){fl('Network error','fer')};x.send(f)}"
"function doReboot(){if(!confirm('Reboot?'))return;"
"var x=new XMLHttpRequest();x.onload=function(){fl('Rebooting...')};"
"x.open('POST','/api/reboot');x.send()}"
"loadStatus();loadCfg();"
"</script></body></html>";

/* ---- STA: GET / ---- */
static esp_err_t sta_root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, DASHBOARD_HTML, -1);
}

/* ---- STA: GET /api/status ---- */
static esp_err_t sta_status_handler(httpd_req_t *req)
{
    /* 设备运行时间 (秒) */
    int64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_s = (uint32_t)(uptime_us / 1000000);

    /* 配置概要 */
    const blackbox_config_t *cfg = config_get_config();
    uint8_t mod_count = cfg ? cfg->module_count : 0;
    uint8_t tgt_count = cfg ? cfg->target_count : 0;
    uint32_t scrape_s = cfg ? cfg->scrape_interval_ms / 1000 : 30;
    uint16_t metrics_port = cfg ? cfg->metrics_port : 9090;

    /* 探测结果 */
    uint8_t res_count = 0;
    const probe_result_t *results = probe_manager_get_results(&res_count);
    uint8_t tgt_count2 = 0;
    const probe_target_t *targets = probe_manager_get_targets(&tgt_count2);

    /* 构建 JSON */
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "fw", app ? app->version : "?");
    cJSON_AddNumberToObject(root, "uptime_s", uptime_s);
    cJSON_AddNumberToObject(root, "modules", mod_count);
    cJSON_AddNumberToObject(root, "targets", tgt_count);
    cJSON_AddNumberToObject(root, "scrape_interval_s", scrape_s);
    cJSON_AddNumberToObject(root, "metrics_port", metrics_port);

    cJSON *res_arr = cJSON_CreateArray();
    for (int i = 0; i < res_count && i < tgt_count2; i++) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "name", targets[i].name);
        cJSON_AddStringToObject(r, "target", targets[i].target);
        cJSON_AddNumberToObject(r, "port", targets[i].port);
        cJSON_AddBoolToObject(r, "success", results[i].success);
        cJSON_AddNumberToObject(r, "duration_ms", results[i].duration_ms);
        if (results[i].error_msg[0] != '\0') {
            cJSON_AddStringToObject(r, "error", results[i].error_msg);
        } else {
            cJSON_AddStringToObject(r, "error", "");
        }
        cJSON_AddItemToArray(res_arr, r);
    }
    cJSON_AddItemToObject(root, "results", res_arr);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ---- STA: GET /api/config ---- */
static esp_err_t sta_config_get_handler(httpd_req_t *req)
{
    const blackbox_config_t *cfg = config_get_config();
    if (!cfg) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No config");
        return ESP_FAIL;
    }

    /* 序列化当前配置为 JSON */
    cJSON *root = cJSON_CreateObject();

    /* 模块 */
    cJSON *modules = cJSON_CreateObject();
    for (int i = 0; i < cfg->module_count; i++) {
        const probe_module_t *m = &cfg->modules[i];
        cJSON *mobj = cJSON_CreateObject();
        const char *prober = "http";
        switch (m->config.type) {
            case MODULE_HTTP:     prober = "http"; break;
            case MODULE_HTTPS:    prober = "https"; break;
            case MODULE_TCP:      prober = "tcp"; break;
            case MODULE_TCP_TLS:  prober = "tcp_tls"; break;
            case MODULE_DNS:      prober = "dns"; break;
            case MODULE_ICMP:     prober = "icmp"; break;
            case MODULE_WS:       prober = "ws"; break;
            case MODULE_WSS:      prober = "wss"; break;
        }
        cJSON_AddStringToObject(mobj, "prober", prober);
        cJSON_AddNumberToObject(mobj, "timeout", m->config.timeout_ms / 1000);
        cJSON_AddItemToObject(modules, m->name, mobj);
    }
    cJSON_AddItemToObject(root, "modules", modules);

    /* 目标 */
    cJSON *targets = cJSON_CreateArray();
    for (int i = 0; i < cfg->target_count; i++) {
        const probe_target_t *t = &cfg->targets[i];
        cJSON *tobj = cJSON_CreateObject();
        cJSON_AddStringToObject(tobj, "name", t->name);
        cJSON_AddStringToObject(tobj, "target", t->target);
        cJSON_AddNumberToObject(tobj, "port", t->port);
        if (t->interval_ms > 0) {
            cJSON_AddNumberToObject(tobj, "interval", t->interval_ms / 1000);
        }
        cJSON_AddStringToObject(tobj, "module", t->module_name);
        cJSON_AddItemToArray(targets, tobj);
    }
    cJSON_AddItemToObject(root, "targets", targets);

    /* 全局配置 */
    cJSON_AddNumberToObject(root, "scrape_interval", cfg->scrape_interval_ms / 1000);
    cJSON_AddNumberToObject(root, "metrics_port", cfg->metrics_port);

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ---- STA: POST /api/config ---- */
static esp_err_t sta_config_post_handler(httpd_req_t *req)
{
    /* 读取请求体 */
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 16384) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    char *buf = malloc(content_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    int recv_len = httpd_req_recv(req, buf, content_len);
    if (recv_len <= 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Read failed");
        return ESP_FAIL;
    }
    buf[recv_len] = '\0';

    esp_err_t err = config_update_targets(buf);
    free(buf);

    if (err != ESP_OK) {
        const char err_resp[] = "{\"status\":\"error\",\"msg\":\"Update failed\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, err_resp, strlen(err_resp));
        return ESP_OK;
    }

    const char ok_resp[] = "{\"status\":\"ok\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, ok_resp, strlen(ok_resp));
    return ESP_OK;
}

/* ---- STA: POST /api/reload ---- */
static esp_err_t sta_reload_handler(httpd_req_t *req)
{
    esp_err_t err = config_reload();

    if (err != ESP_OK) {
        const char err_resp[] = "{\"status\":\"error\",\"msg\":\"Reload failed\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, err_resp, strlen(err_resp));
        return ESP_OK;
    }

    const char ok_resp[] = "{\"status\":\"ok\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, ok_resp, strlen(ok_resp));
    return ESP_OK;
}

/* ================================================================ */
/*  OTA 刷机 / 重启（AP 与 STA 服务器共用，与 s3zero 项目同构）      */
/* ================================================================ */

/* 延迟重启：先把 HTTP 应答发完再 esp_restart（直接重启会吞响应） */
static void web_reboot_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static void web_schedule_reboot(int ms)
{
    static esp_timer_handle_t t;
    if (!t) {
        const esp_timer_create_args_t cfg = {
            .callback = web_reboot_cb,
            .name = "webreboot",
        };
        ESP_ERROR_CHECK(esp_timer_create(&cfg, &t));
    }
    esp_timer_stop(t); /* 幂等：重复触发只取最后一次 */
    esp_timer_start_once(t, (uint64_t)ms * 1000);
}

/* ---- POST /ota：固件流式写备用 OTA 槽 → 校验 → 切槽 → 延迟重启 ----
 * esp_ota_end 做镜像头/哈希校验，失败不切启动槽——原固件无损 */
static esp_err_t ota_handler(httpd_req_t *req)
{
    static char buf[4096]; /* 静态收包缓冲（勿放大栈缓冲） */
    if (req->content_len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"empty body\"}", -1);
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"no ota partition\"}", -1);
    }
    esp_ota_handle_t ota;
    if (esp_ota_begin(part, (size_t)req->content_len, &ota) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"ota begin failed\"}", -1);
    }
    ESP_LOGI(TAG_STA, "OTA 开始：目标槽 %s，%u 字节", part->label,
             (unsigned)req->content_len);
    size_t remain = (size_t)req->content_len;
    while (remain > 0) {
        int got = httpd_req_recv(req, buf, remain > sizeof(buf) ? sizeof(buf) : remain);
        if (got <= 0) {
            esp_ota_abort(ota);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"recv failed\"}", -1);
        }
        if (esp_ota_write(ota, buf, (size_t)got) != ESP_OK) {
            esp_ota_abort(ota);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"ota write failed\"}", -1);
        }
        remain -= (size_t)got;
    }
    if (esp_ota_end(ota) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"image invalid\"}", -1);
    }
    ESP_LOGI(TAG_STA, "OTA 写入并通过校验，1s 后重启进新固件");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\",\"msg\":\"done, rebooting\"}", -1);
    web_schedule_reboot(1000);
    return ESP_OK;
}

/* ---- POST /api/reboot ---- */
static esp_err_t sta_reboot_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", -1);
    web_schedule_reboot(500);
    return ESP_OK;
}

/* ================================================================ */
/*  公共 API                                                        */
/* ================================================================ */

/**
 * @brief 启动 AP 模式配置门户服务器 (端口 80)
 *
 * 注册 GET /, GET /scan, POST /save 路由。
 */
esp_err_t wifi_config_server_start(void)
{
    if (s_ap_server) {
        return ESP_OK;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 4;
    cfg.stack_size = 8192;   /* OTA 收包走静态缓冲，栈适度放宽 */

    if (httpd_start(&s_ap_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG_AP, "AP server start failed");
        return ESP_FAIL;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/",    .method = HTTP_GET,  .handler = ap_root_handler,  .user_ctx = NULL },
        { .uri = "/scan", .method = HTTP_GET,  .handler = ap_scan_handler,  .user_ctx = NULL },
        { .uri = "/save", .method = HTTP_POST, .handler = ap_save_handler,  .user_ctx = NULL },
        { .uri = "/ota", .method = HTTP_POST, .handler = ota_handler,      .user_ctx = NULL },
    };
    for (int i = 0; i < 4; i++) {
        httpd_register_uri_handler(s_ap_server, &uris[i]);
    }

    ESP_LOGI(TAG_AP, "AP config portal started on port 80");
    return ESP_OK;
}

/**
 * @brief 启动 STA 模式配置管理仪表板 (端口 80)
 *
 * 注册 GET /, GET /api/status, GET /api/config,
 * POST /api/config, POST /api/reload 路由。
 */
esp_err_t web_server_start(void)
{
    if (s_sta_server) {
        return ESP_OK;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 7;
    cfg.ctrl_port = 32769;    /* 默认 32768 已被 metrics_server 占用 */
    cfg.stack_size = 8192;    /* OTA 收包走静态缓冲，栈适当放宽 */

    if (httpd_start(&s_sta_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG_STA, "STA web server start failed");
        return ESP_FAIL;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/",           .method = HTTP_GET,  .handler = sta_root_handler,        .user_ctx = NULL },
        { .uri = "/api/status", .method = HTTP_GET,  .handler = sta_status_handler,      .user_ctx = NULL },
        { .uri = "/api/config", .method = HTTP_GET,  .handler = sta_config_get_handler,  .user_ctx = NULL },
        { .uri = "/api/config", .method = HTTP_POST, .handler = sta_config_post_handler, .user_ctx = NULL },
        { .uri = "/api/reload", .method = HTTP_POST, .handler = sta_reload_handler,      .user_ctx = NULL },
        { .uri = "/ota",        .method = HTTP_POST, .handler = ota_handler,             .user_ctx = NULL },
        { .uri = "/api/reboot", .method = HTTP_POST, .handler = sta_reboot_handler,      .user_ctx = NULL },
    };
    for (int i = 0; i < 7; i++) {
        httpd_register_uri_handler(s_sta_server, &uris[i]);
    }

    ESP_LOGI(TAG_STA, "STA web dashboard started on port 80");
    return ESP_OK;
}

/**
 * @brief 停止 STA 模式 Web UI
 */
esp_err_t web_server_stop(void)
{
    if (s_sta_server) {
        httpd_stop(s_sta_server);
        s_sta_server = NULL;
    }
    return ESP_OK;
}
