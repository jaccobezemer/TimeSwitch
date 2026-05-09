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
#include "cJSON.h"
#include <string.h>
#include <time.h>

static const char *TAG = "ota_server";
static httpd_handle_t s_server = NULL;

extern void relay_override_set(uint8_t relay_index, bool on);
extern void relay_override_cancel(uint8_t relay_index);
extern bool relay_override_is_active(uint8_t relay_index);

static esp_err_t status_get_handler(httpd_req_t *req);
static esp_err_t ws_handler(httpd_req_t *req);
static esp_err_t api_relay_post_handler(httpd_req_t *req);
static esp_err_t schedule_get_handler(httpd_req_t *req);
static esp_err_t schedule_edit_get_handler(httpd_req_t *req);
static esp_err_t api_schedule_get_handler(httpd_req_t *req);
static esp_err_t api_schedule_post_handler(httpd_req_t *req);
static esp_err_t names_get_handler(httpd_req_t *req);
static esp_err_t api_names_get_handler(httpd_req_t *req);
static esp_err_t api_names_post_handler(httpd_req_t *req);
static esp_err_t update_get_handler(httpd_req_t *req);
static esp_err_t update_post_handler(httpd_req_t *req);

#define WS_MAX_CLIENTS 4
static int s_ws_fds[WS_MAX_CLIENTS];
static int s_ws_count = 0;

static void ws_add_client(int fd) {
    if (s_ws_count < WS_MAX_CLIENTS) s_ws_fds[s_ws_count++] = fd;
}

static void ws_remove_client(int fd) {
    for (int i = 0; i < s_ws_count; i++) {
        if (s_ws_fds[i] == fd) s_ws_fds[i] = s_ws_fds[--s_ws_count];
    }
}

void ws_broadcast_all_relay_states(void) {
    if (!s_server || s_ws_count == 0) return;
    uint8_t states = relay_get_all_states();
    uint8_t overrides = 0;
    for(int i=0; i<NUM_RELAYS; i++) if (relay_override_is_active(i)) overrides |= (1 << i);

    char buf[64];
    snprintf(buf, sizeof(buf), "{\"states\":%d,\"override\":%d}", states, overrides);

    httpd_ws_frame_t frame = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)buf, .len = strlen(buf) };

    for (int i = 0; i < s_ws_count;) {
        if (httpd_ws_send_frame_async(s_server, s_ws_fds[i], &frame) != ESP_OK) {
            ws_remove_client(s_ws_fds[i]);
        } else {
            i++;
        }
    }
}

static esp_err_t status_get_handler(httpd_req_t *req) {
    const esp_app_desc_t *app = esp_app_get_description();
    time_t now; time(&now); struct tm t; localtime_r(&now, &t);
    char time_str[32]; strftime(time_str, sizeof(time_str), "%d-%m-%Y %H:%M:%S", &t);

    httpd_resp_set_type(req, "text/html");
    static const char HEAD_PRE[] =
        "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>";
    static const char HEAD_POST[] =
        "</title><style>body{font-family:sans-serif;background:#1a1a2e;color:#ccc;margin:0;padding:12px}"
        "h1{color:#00aa44;margin-bottom:4px} .meta{color:#666;font-size:13px;margin-bottom:16px} .links a{color:#00aa44;text-decoration:none;margin-right:20px;font-size:14px}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:12px;margin-top:16px} .relay-box{background:#222233;border-radius:10px;padding:16px;display:flex;align-items:center;justify-content:space-between}"
        ".relay-info{flex-grow:1} .relay-name{font-size:16px;font-weight:bold;color:#fff} .relay-state{font-size:14px;} .relay-ovr{font-size:11px;color:#ff8800;margin-top:4px;min-height:13px}"
        ".relay-btn{padding:10px 24px;border:none;border-radius:6px;font-size:15px;font-weight:600;cursor:pointer;min-width:80px} .btn-on{background:#009e41;color:#fff} .btn-off{background:#c02727;color:#fff}"
        "</style></head><body>";
    char title[64];
    snprintf(title, sizeof(title), "%s - %d Relays", app->project_name, NUM_RELAYS);
    httpd_resp_send_chunk(req, HEAD_PRE, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, title, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, HEAD_POST, HTTPD_RESP_USE_STRLEN);

    char h1[64];
    snprintf(h1, sizeof(h1), "<h1>%s - %d Relays</h1>", app->project_name, NUM_RELAYS);
    httpd_resp_send_chunk(req, h1, HTTPD_RESP_USE_STRLEN);

    char meta[128]; snprintf(meta, sizeof(meta), "<p class='meta'>Version: %s &bull; Time: %s</p>", app->version, time_str);
    httpd_resp_send_chunk(req, meta, HTTPD_RESP_USE_STRLEN);

    const char LINKS[] = "<p class='links'><a href='/schedule'>&#x1F551; Schedule</a><a href='/names'>&#x1F3F7; Edit Names</a><a href='/update'>&#x2B06; Firmware Update</a></p>";
    httpd_resp_send_chunk(req, LINKS, HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(req, "<div class='grid'>", HTTPD_RESP_USE_STRLEN);
    char widget[350];
    const settings_t* settings = settings_get();
    for (int i=0; i<NUM_RELAYS; i++) {
        snprintf(widget, sizeof(widget), "<div class='relay-box'><div class='relay-info'><div class='relay-name'>%s</div><div class='relay-state' id='rs%d'>...</div><div class='relay-ovr' id='ro%d'></div></div><button class='relay-btn' id='rb%d' onclick='toggleRelay(%d)'>...</button></div>", settings->relay_names[i], i, i, i, i);
        httpd_resp_send_chunk(req, widget, strlen(widget));
    }
    httpd_resp_send_chunk(req, "</div>", HTTPD_RESP_USE_STRLEN);

    const char SCRIPT[] = "<script>function applyState(d){for(let i=0;i<8;i++){const on=d.states&(1<<i),ovr=d.override&(1<<i),rs=document.getElementById('rs'+i),ro=document.getElementById('ro'+i),rb=document.getElementById('rb'+i);if(!rs||!ro||!rb)continue;rs.textContent=on?'ON':'OFF';rs.style.color=on?'#00aa44':'#cc2222';ro.textContent=ovr?'Override':'';rb.textContent=on?'OFF':'ON';rb.className=on?'relay-btn btn-off':'relay-btn btn-on';}}function connectWS(){const ws=new WebSocket('ws://'+location.host+'/ws');ws.onmessage=e=>applyState(JSON.parse(e.data));ws.onclose=()=>setTimeout(connectWS,2e3);ws.onerror=()=>ws.close()}function toggleRelay(i){const on=document.getElementById('rs'+i).textContent==='OFF';fetch('/api/relay',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({relay:i,on:on})})}connectWS()</script></body></html>";
    httpd_resp_send_chunk(req, SCRIPT, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t schedule_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    const char *pre = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Schedules</title><style>body{font-family:sans-serif;background:#1a1a2e;color:#ccc;padding:12px}h1{color:#00aa44;margin-bottom:16px}a{text-decoration:none}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:12px}.relay-link{background:#222233;border-radius:10px;padding:20px;text-align:center;color:#fff;font-size:18px;font-weight:bold;transition:background .2s}.relay-link:hover{background:#333344}</style></head><body><h1>Select Relay to Edit Schedule</h1><div class='grid'>";
    httpd_resp_send_chunk(req, pre, HTTPD_RESP_USE_STRLEN);
    char link[128];
    for (int i = 0; i < NUM_RELAYS; i++) {
        snprintf(link, sizeof(link), "<a class='relay-link' href='/schedule-edit?relay=%d'>Relay %d</a>", i, i + 1);
        httpd_resp_send_chunk(req, link, HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_send_chunk(req, "</div></body></html>", HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t schedule_edit_get_handler(httpd_req_t *req) {
    char buf[64];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing relay"); return ESP_FAIL; }
    int relay_index = -1; char param[10];
    if (httpd_query_key_value(buf, "relay", param, sizeof(param)) == ESP_OK) relay_index = atoi(param);
    if (relay_index < 0 || relay_index >= NUM_RELAYS) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid relay"); return ESP_FAIL; }

    const char PAGE[] = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Edit</title><style>*{box-sizing:border-box}body{font-family:sans-serif;background:#1a1a2e;color:#ccc;padding:16px}h1{color:#00aa44;font-size:20px}.day{background:#222;border-radius:10px;padding:14px;margin-bottom:12px}.day-hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:10px}.day-name{font-weight:600;color:#fff}.switch{position:relative;width:44px;height:24px}.switch input{opacity:0;width:0;height:0}.slider{position:absolute;cursor:pointer;inset:0;background:#444;border-radius:24px;transition:.3s}.slider:before{content:'';position:absolute;height:18px;width:18px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.3s}input:checked+.slider{background:#00aa44}input:checked+.slider:before{transform:translateX(20px)}.times{display:flex;gap:12px}.time-field label{font-size:11px;color:#888}.time-field input{width:100%;background:#111;color:#fff;border:1px solid #333;border-radius:6px;padding:8px}button{width:100%;padding:14px;background:#00aa44;color:#fff;border:none;border-radius:8px;font-size:16px;cursor:pointer;margin-top:16px}#msg{text-align:center;margin-top:12px;min-height:20px}.ok{color:#00aa44}.err{color:#c22}.back{color:#00aa44;text-decoration:none;margin-bottom:16px}</style></head><body><a class='back' href='/schedule'>&larr; Back</a>";
    httpd_resp_send_chunk(req, PAGE, HTTPD_RESP_USE_STRLEN);

    char title[128];
    snprintf(title, sizeof(title), "<h1>Edit Schedule: Relay %d</h1><form id='f'>", relay_index + 1);
    httpd_resp_send_chunk(req, title, HTTPD_RESP_USE_STRLEN);

    static const char *DAY_NL[] = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
    char day_html[512];
    for (int i=0; i<7; i++) {
        snprintf(day_html, sizeof(day_html), "<div class='day'><div class='day-hdr'><span class='day-name'>%s</span><label class='switch'><input type='checkbox' name='en%d'><span class='slider'></span></label></div><div class='times'><div class='time-field'><label>ON</label><input type='time' name='on%d'></div><div class='time-field'><label>OFF</label><input type='time' name='off%d'></div></div></div>", DAY_NL[i], i, i, i);
        httpd_resp_send_chunk(req, day_html, HTTPD_RESP_USE_STRLEN);
    }

    char SCRIPT[2048];
    snprintf(SCRIPT, sizeof(SCRIPT), "<button type='submit'>Save</button></form><p id='msg'></p><script>var msg=document.getElementById('msg'),relayIdx=%d;fetch('/api/schedule?relay='+relayIdx).then(r=>r.json()).then(d=>{for(var i=0;i<7;i++){document.getElementsByName('en'+i)[0].checked=d[i].en;document.getElementsByName('on'+i)[0].value=d[i].on;document.getElementsByName('off'+i)[0].value=d[i].off;}}).catch(console.error);document.getElementById('f').onsubmit=function(e){e.preventDefault();var days=[];for(var i=0;i<7;i++)days.push({en:document.getElementsByName('en'+i)[0].checked,on:document.getElementsByName('on'+i)[0].value,off:document.getElementsByName('off'+i)[0].value});fetch('/api/schedule?relay='+relayIdx,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(days)}).then(r=>{msg.className=r.ok?'ok':'err';msg.textContent=r.ok?'Saved!':'Error'}).catch(e=>{msg.className='err';msg.textContent='Error'})};</script></body></html>", relay_index);
    httpd_resp_send_chunk(req, SCRIPT, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t api_schedule_get_handler(httpd_req_t *req) {
    char buf[64]; int relay_index = -1; char param[10];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing relay"); return ESP_FAIL; }
    if (httpd_query_key_value(buf, "relay", param, sizeof(param)) == ESP_OK) relay_index = atoi(param);
    if (relay_index < 0 || relay_index >= NUM_RELAYS) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid relay"); return ESP_FAIL; }

    const relay_schedule_t *schedule = &settings_get()->schedules[relay_index];
    char json_buf[512]; int pos = 0;
    pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "[");
    for (int i = 0; i < 7; i++) {
        pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "%s{\"en\":%s,\"on\":\"%02d:%02d\",\"off\":\"%02d:%02d\"}", i ? "," : "", schedule->days[i].enabled ? "true" : "false", schedule->days[i].on_hour, schedule->days[i].on_min, schedule->days[i].off_hour, schedule->days[i].off_min);
    }
    snprintf(json_buf + pos, sizeof(json_buf) - pos, "]");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json_buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_schedule_post_handler(httpd_req_t *req) {
    char query_buf[64]; int relay_index = -1; char param[10];
    if (httpd_req_get_url_query_str(req, query_buf, sizeof(query_buf)) != ESP_OK) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing relay"); return ESP_FAIL; }
    if (httpd_query_key_value(query_buf, "relay", param, sizeof(param)) == ESP_OK) relay_index = atoi(param);
    if (relay_index < 0 || relay_index >= NUM_RELAYS) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid relay"); return ESP_FAIL; }

    char body[1024]; int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data"); return ESP_FAIL; }
    body[len] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root || !cJSON_IsArray(root) || cJSON_GetArraySize(root) != 7) { cJSON_Delete(root); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected array of 7 days"); return ESP_FAIL; }

    relay_schedule_t sched;
    for(int i=0; i<7; i++) {
        cJSON *day = cJSON_GetArrayItem(root, i);
        sched.days[i].enabled = cJSON_IsTrue(cJSON_GetObjectItem(day, "en"));
        sscanf(cJSON_GetObjectItem(day, "on")->valuestring, "%hhu:%hhu", &sched.days[i].on_hour, &sched.days[i].on_min);
        sscanf(cJSON_GetObjectItem(day, "off")->valuestring, "%hhu:%hhu", &sched.days[i].off_hour, &sched.days[i].off_min);
    }
    cJSON_Delete(root);
    settings_set_schedule_for_relay(relay_index, &sched);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t names_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    static const char PAGE[] =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Relay instellingen</title>"
        "<style>"
        "body{font-family:sans-serif;background:#1a1a2e;color:#ccc;padding:16px}"
        "h1{color:#00aa44}"
        ".relay-row{background:#222233;border-radius:8px;padding:12px;margin-bottom:10px;max-width:480px}"
        ".relay-title{color:#fff;font-weight:bold;margin-bottom:8px}"
        ".field-row{display:grid;grid-template-columns:110px 1fr;align-items:center;gap:8px;margin-bottom:6px}"
        "label{color:#888;font-size:13px}"
        "input,select{width:100%;background:#111;color:#fff;border:1px solid #333;border-radius:6px;padding:8px;box-sizing:border-box}"
        ".pulse-row{display:none}"
        "button{padding:14px 24px;background:#00aa44;color:#fff;border:none;border-radius:8px;font-size:16px;cursor:pointer;margin-top:8px;max-width:480px;width:100%}"
        "#msg{text-align:center;margin-top:12px;min-height:20px}"
        ".ok{color:#00aa44}.err{color:#c22}"
        ".back{color:#00aa44;text-decoration:none;margin-bottom:16px;display:inline-block}"
        "</style></head><body>"
        "<a class='back' href='/'>&larr; Terug</a>"
        "<h1>Relay instellingen</h1>"
        "<form id='f'></form><p id='msg'></p>"
        "<script>"
        "const form=document.getElementById('f'),msg=document.getElementById('msg');"
        "let n=0;"
        "function updatePulse(i){"
        "  const t=parseInt(document.getElementById('type'+i).value);"
        "  document.getElementById('pr'+i).style.display=t===1?'grid':'none';"
        "}"
        "fetch('/api/names').then(r=>r.json()).then(data=>{"
        "  n=data.length;"
        "  data.forEach((d,i)=>{"
        "    const row=document.createElement('div');row.className='relay-row';"
        "    row.innerHTML="
        "      `<div class='relay-title'>Relay ${i+1}</div>`"
        "      +`<div class='field-row'><label>Naam</label><input type='text' id='nm${i}' value='${d.name}' maxlength='31'></div>`"
        "      +`<div class='field-row'><label>Type</label><select id='type${i}' onchange='updatePulse(${i})'>`"
        "        +`<option value='0'${d.type===0?' selected':''}>Normaal</option>`"
        "        +`<option value='1'${d.type===1?' selected':''}>Bistabiel (impuls)</option>`"
        "        +`</select></div>`"
        "      +`<div class='field-row pulse-row' id='pr${i}'><label>Puls (ms)</label>`"
        "        +`<input type='number' id='pm${i}' value='${d.pulse_ms}' min='50' max='5000' step='50'></div>`;"
        "    form.appendChild(row);"
        "    if(d.type===1)document.getElementById('pr'+i).style.display='grid';"
        "  });"
        "  const btn=document.createElement('button');btn.type='submit';btn.textContent='Opslaan';form.appendChild(btn);"
        "});"
        "form.onsubmit=e=>{"
        "  e.preventDefault();"
        "  const data=[];"
        "  for(let i=0;i<n;i++)data.push({"
        "    name:document.getElementById('nm'+i).value,"
        "    type:parseInt(document.getElementById('type'+i).value),"
        "    pulse_ms:parseInt(document.getElementById('pm'+i).value)||1000"
        "  });"
        "  fetch('/api/names',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})"
        "    .then(r=>{msg.className=r.ok?'ok':'err';return r.text();})"
        "    .then(t=>{msg.textContent=t;})"
        "    .catch(()=>{msg.className='err';msg.textContent='Request mislukt';});"
        "};"
        "</script></body></html>";
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_names_get_handler(httpd_req_t *req) {
    const settings_t *s = settings_get();
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < NUM_RELAYS; i++) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name",     s->relay_names[i]);
        cJSON_AddNumberToObject(obj, "type",     s->relay_type[i]);
        cJSON_AddNumberToObject(obj, "pulse_ms", s->relay_pulse_ms[i]);
        cJSON_AddItemToArray(root, obj);
    }
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    return ESP_OK;
}

static esp_err_t api_names_post_handler(httpd_req_t *req) {
    char buf[1024];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data"); return ESP_FAIL; }
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root || !cJSON_IsArray(root) || cJSON_GetArraySize(root) != NUM_RELAYS) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Verwacht array van NUM_RELAYS objecten");
        return ESP_FAIL;
    }

    for (int i = 0; i < NUM_RELAYS; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        cJSON *name_j     = cJSON_GetObjectItem(item, "name");
        cJSON *type_j     = cJSON_GetObjectItem(item, "type");
        cJSON *pulse_ms_j = cJSON_GetObjectItem(item, "pulse_ms");

        if (cJSON_IsString(name_j) && name_j->valuestring)
            settings_set_relay_name(i, name_j->valuestring);

        if (cJSON_IsNumber(type_j) && cJSON_IsNumber(pulse_ms_j)) {
            uint8_t  type     = (uint8_t)type_j->valueint;
            uint16_t pulse_ms = (uint16_t)pulse_ms_j->valueint;
            settings_set_relay_config(i, type, pulse_ms);
            relay_set_config(i, type, pulse_ms);
        }
    }

    cJSON_Delete(root);
    httpd_resp_sendstr(req, "Opgeslagen!");
    return ESP_OK;
}


static const char UPLOAD_PAGE_PRE[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>TimeSwitch OTA</title>"
    "<style>"
    "body{font-family:sans-serif;background:#1a1a2e;color:#ccc;"
    "display:flex;justify-content:center;align-items:center;height:100vh;margin:0}"
    ".box{background:#222233;padding:2em;border-radius:12px;text-align:center;max-width:400px;width:90%}"
    "h2{color:#00aa44;margin-top:0}"
    ".ver{color:#888;font-size:13px;margin-top:-0.5em;margin-bottom:1em}"
    "input[type=file]{margin:1em 0;color:#ccc}"
    "button{background:#00aa44;color:#fff;border:none;padding:12px 32px;"
    "border-radius:6px;font-size:16px;cursor:pointer}"
    "button:disabled{background:#555}"
    "#progress{display:none;margin-top:1em}"
    "#bar{width:100%;height:20px;background:#333;border-radius:10px;overflow:hidden}"
    "#fill{height:100%;width:0%;background:#00aa44;transition:width 0.3s}"
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
    "st.textContent='Gelukt! Apparaat herstart...';fill.style.width='100%';fill.style.background='#00aa44';"
    "setTimeout(function(){st.textContent='Wachten op apparaat...';fill.style.background='#ff8800';"
    "var iv=setInterval(function(){fetch('/update').then(function(){clearInterval(iv);"
    "st.textContent='Apparaat is terug! Herladen...';fill.style.background='#00aa44';"
    "setTimeout(function(){location.href='/';},500);}).catch(function(){});},2000);},4000);}"
    "else{st.textContent='Fout: '+xhr.responseText;fill.style.background='#c02727';btn.disabled=false;}};"
    "xhr.onerror=function(){st.textContent='Upload mislukt';fill.style.background='#c02727';btn.disabled=false;};"
    "xhr.open('POST','/update',true);"
    "xhr.setRequestHeader('Content-Type','application/octet-stream');"
    "xhr.send(f);};"
    "</script></div></body></html>";

static esp_err_t update_get_handler(httpd_req_t *req) {
    const esp_app_desc_t *app = esp_app_get_description();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, UPLOAD_PAGE_PRE, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, app->version, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, UPLOAD_PAGE_POST, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static void ota_restart_cb(void *arg) { (void)arg; esp_restart(); }

static esp_err_t update_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "OTA update gestart, content length: %d", req->content_len);

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        ESP_LOGE(TAG, "Geen OTA partitie gevonden");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t handle;
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char buf[1024];
    int total_read = 0;
    int remaining = req->content_len;

    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf));
        if (n <= 0) {
            if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "Ontvangstfout");
            esp_ota_abort(handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }
        if (esp_ota_write(handle, buf, n) != ESP_OK) {
            ESP_LOGE(TAG, "OTA write mislukt");
            esp_ota_abort(handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }
        total_read += n;
        remaining  -= n;
        if (total_read % (64 * 1024) == 0)
            ESP_LOGI(TAG, "Geschreven %d / %d bytes", total_read, req->content_len);
    }

    if (esp_ota_end(handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation failed");
        return ESP_FAIL;
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA geslaagd (%d bytes), herstarten...", total_read);
    httpd_resp_sendstr(req, "OK");

    const esp_timer_create_args_t timer_args = { .callback = ota_restart_cb, .name = "ota_restart" };
    esp_timer_handle_t timer;
    if (esp_timer_create(&timer_args, &timer) == ESP_OK)
        esp_timer_start_once(timer, 2000000);
    else
        esp_restart();

    return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        ws_add_client(fd);
        ws_broadcast_all_relay_states();
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ws_remove_client(fd);
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ws_remove_client(fd);
        return ESP_OK;
    }

    if (ws_pkt.len) {
        uint8_t *buf = calloc(1, ws_pkt.len + 1);
        if (!buf) return ESP_ERR_NO_MEM;
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        free(buf);
        if (ret != ESP_OK) {
            ws_remove_client(fd);
            return ret;
        }
    }

    return ESP_OK;
}

static esp_err_t api_relay_post_handler(httpd_req_t *req) {
    char buf[64]; int len = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = 0;
    cJSON *json = cJSON_Parse(buf);
    int idx = cJSON_GetObjectItem(json, "relay")->valueint;
    bool on = cJSON_GetObjectItem(json, "on")->valueint;
    cJSON_Delete(json);
    relay_override_set(idx, on);
    ws_broadcast_all_relay_states();
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

esp_err_t ota_server_start(void) {
    if (s_server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 1024 * 10; config.stack_size = 8192; config.max_uri_handlers = 15;
    if (httpd_start(&s_server, &config) != ESP_OK) return ESP_FAIL;

    httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = status_get_handler},
        {.uri = "/update", .method = HTTP_GET, .handler = update_get_handler},
        {.uri = "/update", .method = HTTP_POST, .handler = update_post_handler},
        {.uri = "/schedule", .method = HTTP_GET, .handler = schedule_get_handler},
        {.uri = "/schedule-edit", .method = HTTP_GET, .handler = schedule_edit_get_handler},
        {.uri = "/api/schedule", .method = HTTP_GET, .handler = api_schedule_get_handler},
        {.uri = "/api/schedule", .method = HTTP_POST, .handler = api_schedule_post_handler},
        {.uri = "/api/relay", .method = HTTP_POST, .handler = api_relay_post_handler},
        {.uri = "/names", .method = HTTP_GET, .handler = names_get_handler},
        {.uri = "/api/names", .method = HTTP_GET, .handler = api_names_get_handler},
        {.uri = "/api/names", .method = HTTP_POST, .handler = api_names_post_handler},
        {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true, .handle_ws_control_frames = true}
    };
    for (int i = 0; i < sizeof(uris)/sizeof(uris[0]); i++) httpd_register_uri_handler(s_server, &uris[i]);
    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}
