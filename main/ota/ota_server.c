#include "ota_server.h"
#include "settings.h"
#include "relay.h"
#include "time_sync.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <string.h>
#include <stdatomic.h>
#include <time.h>

static const char *TAG = "ota_server";
static httpd_handle_t s_server = NULL;
static atomic_int s_ota_progress = -1;

extern void relay_override_set(bool on);
extern void relay_override_cancel(void);
extern bool relay_override_is_active(void);
extern bool relay_override_base_state(void);
extern void ui_main_update_relay(bool on);

/* ── WebSocket client tracking ───────────────────────────────────────── */
#define WS_MAX_CLIENTS 4
static int s_ws_fds[WS_MAX_CLIENTS];
static int s_ws_count = 0;

static void ws_add_client(int fd)
{
    for (int i = 0; i < s_ws_count; i++) {
        if (s_ws_fds[i] == fd) return;
    }
    if (s_ws_count < WS_MAX_CLIENTS) {
        s_ws_fds[s_ws_count++] = fd;
    }
}

static void ws_remove_client(int fd)
{
    for (int i = 0; i < s_ws_count; i++) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = s_ws_fds[--s_ws_count];
            return;
        }
    }
}

void ws_broadcast_relay_state(void)
{
    if (!s_server || s_ws_count == 0) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "{\"on\":%s,\"override\":%s}",
             relay_get()                ? "true" : "false",
             relay_override_is_active() ? "true" : "false");

    httpd_ws_frame_t frame = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)buf,
        .len     = strlen(buf),
    };

    int i = 0;
    while (i < s_ws_count) {
        bool connected = (httpd_ws_get_fd_info(s_server, s_ws_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET);
        if (!connected) {
            ws_remove_client(s_ws_fds[i]);
            continue;
        }
        esp_err_t err = httpd_ws_send_frame_async(s_server, s_ws_fds[i], &frame);
        if (err != ESP_OK) {
            ws_remove_client(s_ws_fds[i]);
        } else {
            i++;
        }
    }
}

int ota_get_progress(void)
{
    return atomic_load(&s_ota_progress);
}

/* ── GET / — statuspagina ───────────────────────────────────────────── */
static esp_err_t status_get_handler(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%d-%m-%Y %H:%M:%S", &t);

    const settings_t *cfg = settings_get();

    static const char HEAD[] =
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        ""
        "<title>TimeSwitch</title>"
        "<style>"
        "body{font-family:sans-serif;background:#1a1a2e;color:#ccc;margin:0;padding:20px}"
        "h1{color:#00aa44;margin-bottom:4px}"
        ".meta{color:#666;font-size:13px;margin-bottom:16px}"
        ".links a{color:#00aa44;text-decoration:none;margin-right:20px;font-size:14px}"
        "table{border-collapse:collapse;width:100%;max-width:400px;margin-top:16px}"
        "th{background:#222233;color:#aaa;padding:8px 14px;text-align:left;font-size:13px}"
        "td{padding:8px 14px;border-bottom:1px solid #222233;font-size:13px}"
        ".on{color:#00aa44;font-weight:bold}"
        ".off{color:#888}"
        ".relay-box{background:#222233;border-radius:10px;padding:16px;max-width:400px;"
        "margin-top:16px;display:flex;align-items:center;justify-content:space-between}"
        ".relay-state{font-size:18px;font-weight:bold}"
        ".relay-ovr{font-size:12px;color:#ff8800;margin-top:4px}"
        ".relay-btn{padding:10px 24px;border:none;border-radius:6px;font-size:15px;"
        "font-weight:600;cursor:pointer;min-width:80px}"
        ".btn-on{background:#00aa44;color:#fff}"
        ".btn-off{background:#cc2222;color:#fff}"
        "</style></head><body>"
        "<h1>TimeSwitch</h1>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, HEAD, HTTPD_RESP_USE_STRLEN);

    char meta[128];
    snprintf(meta, sizeof(meta),
             "<p class='meta'>Versie: %s &bull; Tijd: %s</p>",
             app->version, time_str);
    httpd_resp_send_chunk(req, meta, HTTPD_RESP_USE_STRLEN);

    static const char LINKS[] =
        "<p class='links'>"
        "<a href='/schedule'>&#x1F551; Schema instellen</a>"
        "<a href='/update'>&#x2B06; Firmware update</a>"
        "</p>";
    httpd_resp_send_chunk(req, LINKS, HTTPD_RESP_USE_STRLEN);

    // Relais widget
    static const char RELAY_WIDGET[] =
        "<div class='relay-box'>"
        "<div>"
        "<div class='relay-state' id='rs'>...</div>"
        "<div class='relay-ovr' id='ro'></div>"
        "</div>"
        "<button class='relay-btn' id='rb' onclick='toggleRelay()'>...</button>"
        "</div>"
        "<script>"
        "var ws;"
        "var _on=false;"
        "function applyState(d){"
        "var rs=document.getElementById('rs');"
        "var ro=document.getElementById('ro');"
        "var rb=document.getElementById('rb');"
        "_on=d.on;"
        "rs.textContent=d.on?'Relais: AAN':'Relais: UIT';"
        "rs.style.color=d.on?'#00aa44':'#cc2222';"
        "ro.textContent=d.override?'Override actief':'';"
        "rb.textContent=d.on?'Zet UIT':'Zet AAN';"
        "rb.className=d.on?'relay-btn btn-off':'relay-btn btn-on';}"
        "function connectWS(){"
        "ws=new WebSocket('ws://'+location.host+'/ws');"
        "ws.onmessage=function(e){applyState(JSON.parse(e.data));};"
        "ws.onclose=function(){setTimeout(connectWS,2000);};"
        "ws.onerror=function(){ws.close();};}"
        "function toggleRelay(){"
        "fetch('/api/relay',{method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({on:!_on})}).catch(function(){});}"
        "connectWS();"
        "</script>";
    httpd_resp_send_chunk(req, RELAY_WIDGET, HTTPD_RESP_USE_STRLEN);

    static const char TABLE_HEAD[] =
        "<table><thead><tr><th>Dag</th><th>Schema</th></tr></thead><tbody>";
    httpd_resp_send_chunk(req, TABLE_HEAD, HTTPD_RESP_USE_STRLEN);

    char row[256];

    // Schema per dag
    static const char *DAY_NL[] = {"Ma","Di","Wo","Do","Vr","Za","Zo"};
    for (int i = 0; i < 7; i++) {
        if (cfg->days[i].enabled) {
            snprintf(row, sizeof(row),
                     "<tr><td>%s</td><td>%02d:%02d &ndash; %02d:%02d</td></tr>",
                     DAY_NL[i],
                     cfg->days[i].on_hour,  cfg->days[i].on_min,
                     cfg->days[i].off_hour, cfg->days[i].off_min);
            httpd_resp_send_chunk(req, row, HTTPD_RESP_USE_STRLEN);
        }
    }

    static const char TAIL[] = "</tbody></table></body></html>";
    httpd_resp_send_chunk(req, TAIL, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* ── GET /update — OTA uploadpagina ────────────────────────────────── */
static const char UPLOAD_PAGE_PRE[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>TimeSwitch OTA Update</title>"
    "<style>"
    "body{font-family:sans-serif;background:#1a1a2e;color:#ccc;"
    "display:flex;justify-content:center;align-items:center;height:100vh;margin:0}"
    ".box{background:#222233;padding:2em;border-radius:12px;text-align:center;max-width:400px;width:90%%}"
    "h2{color:#00aa44;margin-top:0}"
    ".ver{color:#888;font-size:13px;margin-top:-0.5em;margin-bottom:1em}"
    "input[type=file]{margin:1em 0;color:#ccc}"
    "button{background:#00aa44;color:#fff;border:none;padding:12px 32px;"
    "border-radius:6px;font-size:16px;cursor:pointer}"
    "button:disabled{background:#555}"
    "#progress{display:none;margin-top:1em}"
    "#bar{width:100%%;height:20px;background:#333;border-radius:10px;overflow:hidden}"
    "#fill{height:100%%;width:0%%;background:#00aa44;transition:width 0.3s}"
    "#status{margin-top:0.5em;font-size:14px}"
    "</style></head><body><div class='box'>"
    "<h2>TimeSwitch OTA Update</h2>"
    "<p class='ver'>Huidige firmware: ";

static const char UPLOAD_PAGE_POST[] =
    "</p>"
    "<form id='f'><input type='file' id='fw' accept='.bin'><br>"
    "<button type='submit' id='btn'>Upload Firmware</button></form>"
    "<div id='progress'><div id='bar'><div id='fill'></div></div>"
    "<div id='status'>Uploading...</div></div>"
    "<script>"
    "document.getElementById('f').onsubmit=function(e){"
    "e.preventDefault();"
    "var f=document.getElementById('fw').files[0];"
    "if(!f){alert('Selecteer een .bin bestand');return;}"
    "var xhr=new XMLHttpRequest();"
    "var p=document.getElementById('progress');"
    "var fill=document.getElementById('fill');"
    "var st=document.getElementById('status');"
    "var btn=document.getElementById('btn');"
    "btn.disabled=true;p.style.display='block';"
    "xhr.upload.onprogress=function(e){"
    "if(e.lengthComputable){var pct=Math.round(e.loaded/e.total*100);"
    "fill.style.width=pct+'%';st.textContent='Uploading: '+pct+'%';}};"
    "xhr.onload=function(){"
    "if(xhr.status==200){"
    "st.textContent='Klaar! Herstart...';fill.style.width='100%';fill.style.background='#00aa44';"
    "setTimeout(function(){"
    "st.textContent='Wachten op apparaat...';"
    "var iv=setInterval(function(){fetch('/').then(function(){clearInterval(iv);"
    "st.textContent='Apparaat is terug! Doorsturen...';fill.style.background='#00aa44';"
    "setTimeout(function(){location.href='/';},500);}).catch(function(){});},2000);"
    "},4000);}"
    "else{st.textContent='Fout: '+xhr.responseText;fill.style.background='#cc2222';btn.disabled=false;}};"
    "xhr.onerror=function(){st.textContent='Upload mislukt';fill.style.background='#cc2222';btn.disabled=false;};"
    "xhr.open('POST','/update',true);"
    "xhr.setRequestHeader('Content-Type','application/octet-stream');"
    "xhr.send(f);};"
    "</script></div></body></html>";

static esp_err_t update_get_handler(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, UPLOAD_PAGE_PRE, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, app->version, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, UPLOAD_PAGE_POST, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* ── POST /update — firmware ontvangen en flashen ───────────────────── */
static void ota_restart_cb(void *arg)
{
    esp_restart();
}

static esp_err_t update_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA gestart, %d bytes", req->content_len);
    atomic_store(&s_ota_progress, 0);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "Geen OTA partitie gevonden");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        atomic_store(&s_ota_progress, -1);
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        atomic_store(&s_ota_progress, -1);
        return ESP_FAIL;
    }

    char buf[1024];
    int total = 0, remaining = req->content_len;

    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, sizeof(buf) < remaining ? sizeof(buf) : remaining);
        if (n <= 0) {
            if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            atomic_store(&s_ota_progress, -1);
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, n);
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            atomic_store(&s_ota_progress, -1);
            return ESP_FAIL;
        }

        total     += n;
        remaining -= n;
        atomic_store(&s_ota_progress, (int)((int64_t)total * 100 / req->content_len));
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation failed");
        atomic_store(&s_ota_progress, -1);
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        atomic_store(&s_ota_progress, -1);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA klaar (%d bytes), herstart...", total);
    atomic_store(&s_ota_progress, 101);
    httpd_resp_sendstr(req, "OK");

    const esp_timer_create_args_t restart_args = { .callback = ota_restart_cb, .name = "ota_rst" };
    esp_timer_handle_t restart_timer;
    if (esp_timer_create(&restart_args, &restart_timer) == ESP_OK) {
        esp_timer_start_once(restart_timer, 2000 * 1000);
    } else {
        esp_restart();
    }

    return ESP_OK;
}

/* ── GET /schedule — schema webpagina ──────────────────────────────── */
static const char SCHEDULE_PAGE[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>TimeSwitch Schema</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
    "background:#1a1a2e;color:#ccc;padding:16px}"
    "h1{color:#00aa44;margin-bottom:4px;font-size:20px}"
    ".sub{color:#666;font-size:13px;margin-bottom:20px}"
    ".day{background:#222233;border-radius:10px;padding:14px;margin-bottom:12px}"
    ".day-hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:10px}"
    ".day-name{font-weight:600;font-size:15px;color:#fff}"
    ".switch{position:relative;width:44px;height:24px}"
    ".switch input{opacity:0;width:0;height:0}"
    ".slider{position:absolute;cursor:pointer;inset:0;background:#444;"
    "border-radius:24px;transition:.3s}"
    ".slider:before{content:'';position:absolute;height:18px;width:18px;"
    "left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.3s}"
    "input:checked+.slider{background:#00aa44}"
    "input:checked+.slider:before{transform:translateX(20px)}"
    ".times{display:flex;gap:12px}"
    ".time-field{flex:1}"
    ".time-field label{display:block;font-size:11px;color:#888;margin-bottom:4px;text-transform:uppercase}"
    ".time-field input{width:100%;background:#111122;color:#fff;border:1px solid #333;"
    "border-radius:6px;padding:8px;font-size:16px}"
    ".time-field input:focus{outline:none;border-color:#00aa44}"
    "button{width:100%;padding:14px;background:#00aa44;color:#fff;border:none;"
    "border-radius:8px;font-size:16px;font-weight:600;cursor:pointer;margin-top:16px}"
    "button:active{background:#008833}"
    "#msg{text-align:center;margin-top:12px;font-size:14px;min-height:20px}"
    ".ok{color:#00aa44}.err{color:#cc2222}"
    ".back{display:inline-block;color:#00aa44;text-decoration:none;font-size:14px;margin-bottom:16px}"
    "</style></head><body>"
    "<a class='back' href='/'>&#8592; Terug</a>"
    "<h1>Schakelklok Schema</h1>"
    "<p class='sub'>Schema wordt per weekdag opgeslagen. "
    "Druk op het display om tijdelijk te overriden.</p>"
    "<form id='f'>"
    "<div class='day'><div class='day-hdr'>"
    "<span class='day-name'>Maandag</span>"
    "<label class='switch'><input type='checkbox' name='en0' id='en0'><span class='slider'></span></label>"
    "</div><div class='times'>"
    "<div class='time-field'><label>Aan om</label><input type='time' name='on0' value='07:00'></div>"
    "<div class='time-field'><label>Uit om</label><input type='time' name='off0' value='23:00'></div>"
    "</div></div>"
    "<div class='day'><div class='day-hdr'>"
    "<span class='day-name'>Dinsdag</span>"
    "<label class='switch'><input type='checkbox' name='en1' id='en1'><span class='slider'></span></label>"
    "</div><div class='times'>"
    "<div class='time-field'><label>Aan om</label><input type='time' name='on1' value='07:00'></div>"
    "<div class='time-field'><label>Uit om</label><input type='time' name='off1' value='23:00'></div>"
    "</div></div>"
    "<div class='day'><div class='day-hdr'>"
    "<span class='day-name'>Woensdag</span>"
    "<label class='switch'><input type='checkbox' name='en2' id='en2'><span class='slider'></span></label>"
    "</div><div class='times'>"
    "<div class='time-field'><label>Aan om</label><input type='time' name='on2' value='07:00'></div>"
    "<div class='time-field'><label>Uit om</label><input type='time' name='off2' value='23:00'></div>"
    "</div></div>"
    "<div class='day'><div class='day-hdr'>"
    "<span class='day-name'>Donderdag</span>"
    "<label class='switch'><input type='checkbox' name='en3' id='en3'><span class='slider'></span></label>"
    "</div><div class='times'>"
    "<div class='time-field'><label>Aan om</label><input type='time' name='on3' value='07:00'></div>"
    "<div class='time-field'><label>Uit om</label><input type='time' name='off3' value='23:00'></div>"
    "</div></div>"
    "<div class='day'><div class='day-hdr'>"
    "<span class='day-name'>Vrijdag</span>"
    "<label class='switch'><input type='checkbox' name='en4' id='en4'><span class='slider'></span></label>"
    "</div><div class='times'>"
    "<div class='time-field'><label>Aan om</label><input type='time' name='on4' value='07:00'></div>"
    "<div class='time-field'><label>Uit om</label><input type='time' name='off4' value='23:00'></div>"
    "</div></div>"
    "<div class='day'><div class='day-hdr'>"
    "<span class='day-name'>Zaterdag</span>"
    "<label class='switch'><input type='checkbox' name='en5' id='en5'><span class='slider'></span></label>"
    "</div><div class='times'>"
    "<div class='time-field'><label>Aan om</label><input type='time' name='on5' value='07:00'></div>"
    "<div class='time-field'><label>Uit om</label><input type='time' name='off5' value='23:00'></div>"
    "</div></div>"
    "<div class='day'><div class='day-hdr'>"
    "<span class='day-name'>Zondag</span>"
    "<label class='switch'><input type='checkbox' name='en6' id='en6'><span class='slider'></span></label>"
    "</div><div class='times'>"
    "<div class='time-field'><label>Aan om</label><input type='time' name='on6' value='07:00'></div>"
    "<div class='time-field'><label>Uit om</label><input type='time' name='off6' value='23:00'></div>"
    "</div></div>"
    "<button type='submit'>Opslaan</button>"
    "</form>"
    "<p id='msg'></p>"
    "<script>"
    "var msg=document.getElementById('msg');"
    // Laad huidig schema
    "fetch('/api/schedule').then(function(r){return r.json();}).then(function(d){"
    "for(var i=0;i<7;i++){"
    "document.getElementsByName('en'+i)[0].checked=d[i].en;"
    "document.getElementsByName('on'+i)[0].value=d[i].on;"
    "document.getElementsByName('off'+i)[0].value=d[i].off;"
    "}}).catch(function(){});"
    // Opslaan
    "document.getElementById('f').onsubmit=function(e){"
    "e.preventDefault();"
    "var days=[];"
    "for(var i=0;i<7;i++){"
    "days.push({"
    "en:document.getElementsByName('en'+i)[0].checked,"
    "on:document.getElementsByName('on'+i)[0].value,"
    "off:document.getElementsByName('off'+i)[0].value"
    "});}"
    "fetch('/api/schedule',{method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify(days)})"
    ".then(function(r){if(r.ok){msg.className='ok';msg.textContent='Opgeslagen!';}"
    "else{msg.className='err';msg.textContent='Fout bij opslaan';}}"
    ").catch(function(){msg.className='err';msg.textContent='Verbindingsfout';});"
    "};"
    "</script></body></html>";

static esp_err_t schedule_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, SCHEDULE_PAGE);
    return ESP_OK;
}

/* ── GET /api/schedule — huidig schema als JSON ─────────────────────── */
static esp_err_t api_schedule_get_handler(httpd_req_t *req)
{
    const settings_t *cfg = settings_get();
    char buf[512];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "[");
    for (int i = 0; i < 7; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%s{\"en\":%s,\"on\":\"%02d:%02d\",\"off\":\"%02d:%02d\"}",
                        i > 0 ? "," : "",
                        cfg->days[i].enabled ? "true" : "false",
                        cfg->days[i].on_hour,  cfg->days[i].on_min,
                        cfg->days[i].off_hour, cfg->days[i].off_min);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* ── POST /api/schedule — schema opslaan ───────────────────────────── */
static esp_err_t api_schedule_post_handler(httpd_req_t *req)
{
    char body[512] = {0};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    body[len] = '\0';

    // Parse JSON array: [{en:bool,on:"HH:MM",off:"HH:MM"}, ...]
    day_schedule_t days[7];
    memset(days, 0, sizeof(days));

    const char *p = body;
    for (int i = 0; i < 7; i++) {
        // Zoek "en": true/false
        char *en_p = strstr(p, "\"en\":");
        if (!en_p) break;
        en_p += 5;
        days[i].enabled = (strncmp(en_p, "true", 4) == 0);

        // Zoek "on":"HH:MM"
        char *on_p = strstr(en_p, "\"on\":\"");
        if (!on_p) break;
        on_p += 6;
        int on_h, on_m;
        if (sscanf(on_p, "%d:%d", &on_h, &on_m) == 2) {
            days[i].on_hour = (uint8_t)on_h;
            days[i].on_min  = (uint8_t)on_m;
        }

        // Zoek "off":"HH:MM"
        char *off_p = strstr(on_p, "\"off\":\"");
        if (!off_p) break;
        off_p += 7;
        int off_h, off_m;
        if (sscanf(off_p, "%d:%d", &off_h, &off_m) == 2) {
            days[i].off_hour = (uint8_t)off_h;
            days[i].off_min  = (uint8_t)off_m;
        }

        // Volgende dag
        char *next = strchr(off_p, '}');
        if (!next) break;
        p = next + 1;
    }

    settings_set_schedule(days);
    ESP_LOGI(TAG, "Schema opgeslagen via web");

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* ── WebSocket /ws ──────────────────────────────────────────────────── */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // Nieuwe verbinding
        int fd = httpd_req_to_sockfd(req);
        ws_add_client(fd);
        ESP_LOGI(TAG, "WS client verbonden: fd=%d", fd);
        // Stuur meteen de huidige staat
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"on\":%s,\"override\":%s}",
                 relay_get()                ? "true" : "false",
                 relay_override_is_active() ? "true" : "false");
        httpd_ws_frame_t frame = {
            .type    = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)buf,
            .len     = strlen(buf),
        };
        httpd_ws_send_frame(req, &frame);
        return ESP_OK;
    }

    int fd = httpd_req_to_sockfd(req);
    httpd_ws_frame_t frame = {0};
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ws_remove_client(fd);
        return ret;
    }
    if (frame.len > 0) {
        uint8_t *buf = calloc(1, frame.len + 1);
        if (buf) {
            frame.payload = buf;
            httpd_ws_recv_frame(req, &frame, frame.len);
            free(buf);
        }
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE || frame.type == HTTPD_WS_TYPE_PONG) {
        // control frames worden nu afgehandeld door de server zelf
    }
    return ESP_OK;
}

/* ── GET /api/relay — huidige relaisstatus als JSON ─────────────────── */
static esp_err_t api_relay_get_handler(httpd_req_t *req)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"on\":%s,\"override\":%s}",
             relay_get()                ? "true" : "false",
             relay_override_is_active() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* ── POST /api/relay — relais aan/uit zetten ────────────────────────── */
static esp_err_t api_relay_post_handler(httpd_req_t *req)
{
    char body[32] = {0};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    body[len] = '\0';

    bool on;
    if (strstr(body, "\"on\":true"))       on = true;
    else if (strstr(body, "\"on\":false")) on = false;
    else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    // Zelfde logica als de touchscreen-knop:
    // als override actief is en de nieuwe staat == schema-basisstaat → override opheffen
    if (relay_override_is_active()) {
        if (on == relay_override_base_state()) {
            relay_set(on);
            relay_override_cancel();
            ui_main_update_relay(on);
            ws_broadcast_relay_state();
            ESP_LOGI(TAG, "Relais via web terug naar schema-staat, override opgeheven");
        } else if (on != relay_get()) {
            relay_override_set(on);
            ws_broadcast_relay_state();
            ESP_LOGI(TAG, "Relais via web %s (override)", on ? "AAN" : "UIT");
        }
    } else {
        if (on != relay_get()) {
            relay_override_set(on);
            ws_broadcast_relay_state();
            ESP_LOGI(TAG, "Relais via web %s (override)", on ? "AAN" : "UIT");
        }
    }

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* ── Start ──────────────────────────────────────────────────────────── */
esp_err_t ota_server_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.stack_size       = 8192;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 11;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server starten mislukt: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t uris[] = {
        { .uri = "/",             .method = HTTP_GET,  .handler = status_get_handler,        .is_websocket = false },
        { .uri = "/update",       .method = HTTP_GET,  .handler = update_get_handler,        .is_websocket = false },
        { .uri = "/update",       .method = HTTP_POST, .handler = update_post_handler,       .is_websocket = false },
        { .uri = "/schedule",     .method = HTTP_GET,  .handler = schedule_get_handler,      .is_websocket = false },
        { .uri = "/api/schedule", .method = HTTP_GET,  .handler = api_schedule_get_handler,  .is_websocket = false },
        { .uri = "/api/schedule", .method = HTTP_POST, .handler = api_schedule_post_handler, .is_websocket = false },
        { .uri = "/api/relay",    .method = HTTP_GET,  .handler = api_relay_get_handler,     .is_websocket = false },
        { .uri = "/api/relay",    .method = HTTP_POST, .handler = api_relay_post_handler,    .is_websocket = false },
        { .uri = "/ws",           .method = HTTP_GET,  .handler = ws_handler,                .is_websocket = true, .handle_ws_control_frames = true },
    };
    for (int i = 0; i < 9; i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }

    ESP_LOGI(TAG, "Server gestart op poort 80");
    return ESP_OK;
}
