#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gale.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// HTML page content (embedded)
static const char index_html[] =
"<!DOCTYPE html>"
"<html>"
"<head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
"<title>Gale Configuration</title>"
"<style>"
"* { box-sizing: border-box; margin: 0; padding: 0; }"
"body { font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif; background: #f5f5f7; padding: 20px; line-height: 1.6; }"
".container { max-width: 800px; margin: 0 auto; background: white; border-radius: 12px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); padding: 30px; }"
"h1 { color: #1d1d1f; margin-bottom: 10px; font-size: 32px; }"
".subtitle { color: #86868b; margin-bottom: 30px; }"
".section { margin-bottom: 30px; padding-bottom: 30px; border-bottom: 1px solid #d2d2d7; }"
".section:last-child { border-bottom: none; }"
"h2 { color: #1d1d1f; margin-bottom: 15px; font-size: 22px; }"
".form-group { margin-bottom: 20px; }"
"label { display: block; margin-bottom: 5px; color: #1d1d1f; font-weight: 500; }"
".help-text { font-size: 13px; color: #86868b; margin-top: 4px; }"
"input[type=\"text\"], input[type=\"password\"], input[type=\"number\"] { width: 100%; padding: 12px; border: 1px solid #d2d2d7; border-radius: 8px; font-size: 16px; transition: border-color 0.2s; }"
"input:focus { outline: none; border-color: #0071e3; }"
".checkbox-group { display: flex; align-items: center; gap: 10px; }"
"input[type=\"checkbox\"] { width: 20px; height: 20px; cursor: pointer; }"
"button { background: #0071e3; color: white; border: none; padding: 12px 24px; border-radius: 8px; font-size: 16px; font-weight: 500; cursor: pointer; transition: background 0.2s; }"
"button:hover { background: #0077ed; }"
"button:active { background: #006edb; }"
".status { margin-top: 20px; padding: 12px; border-radius: 8px; display: none; }"
".status.success { background: #d1f4e0; color: #03543f; display: block; }"
".status.error { background: #fde8e8; color: #9b1c1c; display: block; }"
".row { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }"
"@media (max-width: 600px) { .row { grid-template-columns: 1fr; } }"
"</style>"
"</head>"
"<body>"
"<div class=\"container\">"
"<h1>Gale</h1>"
"<p class=\"subtitle\">Heart Rate Controlled Fan Configuration</p>"
"<form id=\"configForm\">"
"<div class=\"section\">"
"<h2>WiFi Settings</h2>"
"<div class=\"form-group\">"
"<label for=\"apSSID\">Access Point Name</label>"
"<input type=\"text\" id=\"apSSID\" name=\"apSSID\" required>"
"<div class=\"help-text\">Name of the WiFi network Gale creates</div>"
"</div>"
"<div class=\"form-group\">"
"<label for=\"apPassword\">Access Point Password</label>"
"<input type=\"password\" id=\"apPassword\" name=\"apPassword\" minlength=\"8\" required>"
"<div class=\"help-text\">Password must be at least 8 characters</div>"
"</div>"
"<div class=\"form-group\">"
"<div class=\"checkbox-group\">"
"<input type=\"checkbox\" id=\"useStationMode\" name=\"useStationMode\">"
"<label for=\"useStationMode\" style=\"margin-bottom: 0;\">Connect to existing WiFi network</label>"
"</div>"
"</div>"
"<div id=\"stationFields\" style=\"display: none;\">"
"<div class=\"form-group\">"
"<label for=\"wifiSSID\">WiFi Network Name</label>"
"<input type=\"text\" id=\"wifiSSID\" name=\"wifiSSID\">"
"</div>"
"<div class=\"form-group\">"
"<label for=\"wifiPassword\">WiFi Password</label>"
"<input type=\"password\" id=\"wifiPassword\" name=\"wifiPassword\">"
"</div>"
"</div>"
"</div>"
"<div class=\"section\">"
"<h2>Heart Rate Zones</h2>"
"<div class=\"row\">"
"<div class=\"form-group\">"
"<label for=\"hrMax\">Maximum Heart Rate (BPM)</label>"
"<input type=\"number\" id=\"hrMax\" name=\"hrMax\" min=\"100\" max=\"250\" required>"
"</div>"
"<div class=\"form-group\">"
"<label for=\"hrResting\">Resting Heart Rate (BPM)</label>"
"<input type=\"number\" id=\"hrResting\" name=\"hrResting\" min=\"30\" max=\"100\" required>"
"</div>"
"</div>"
"<div class=\"help-text\">Zone 1: <span id=\"zone1Display\">-</span> BPM | Zone 2: <span id=\"zone2Display\">-</span> BPM | Zone 3: <span id=\"zone3Display\">-</span> BPM</div>"
"</div>"
"<div class=\"section\">"
"<h2>Fan Behavior</h2>"
"<div class=\"form-group\">"
"<div class=\"checkbox-group\">"
"<input type=\"checkbox\" id=\"alwaysOn\" name=\"alwaysOn\">"
"<label for=\"alwaysOn\" style=\"margin-bottom: 0;\">Keep fan on when heart rate is below Zone 1</label>"
"</div>"
"</div>"
"<div class=\"row\">"
"<div class=\"form-group\">"
"<label for=\"fanDelay\">Speed Change Delay (seconds)</label>"
"<input type=\"number\" id=\"fanDelay\" name=\"fanDelay\" min=\"0\" max=\"600\" required>"
"<div class=\"help-text\">Delay before reducing fan speed</div>"
"</div>"
"<div class=\"form-group\">"
"<label for=\"hrHysteresis\">Hysteresis (BPM)</label>"
"<input type=\"number\" id=\"hrHysteresis\" name=\"hrHysteresis\" min=\"0\" max=\"30\" required>"
"<div class=\"help-text\">Prevents rapid speed changes</div>"
"</div>"
"</div>"
"</div>"
"<button type=\"submit\">Save Configuration</button>"
"<div id=\"status\" class=\"status\"></div>"
"</form>"
"</div>"
"<script>"
"fetch('/api/config').then(r=>r.json()).then(data=>{"
"document.getElementById('apSSID').value=data.apSSID;"
"document.getElementById('apPassword').value=data.apPassword;"
"document.getElementById('useStationMode').checked=data.useStationMode;"
"document.getElementById('wifiSSID').value=data.wifiSSID;"
"document.getElementById('wifiPassword').value=data.wifiPassword;"
"document.getElementById('hrMax').value=data.hrMax;"
"document.getElementById('hrResting').value=data.hrResting;"
"document.getElementById('alwaysOn').checked=data.alwaysOn==1;"
"document.getElementById('fanDelay').value=data.fanDelay/1000;"
"document.getElementById('hrHysteresis').value=data.hrHysteresis;"
"updateZoneDisplay();toggleStationFields();"
"});"
"document.getElementById('useStationMode').addEventListener('change',toggleStationFields);"
"function toggleStationFields(){"
"const stationFields=document.getElementById('stationFields');"
"stationFields.style.display=document.getElementById('useStationMode').checked?'block':'none';"
"}"
"document.getElementById('hrMax').addEventListener('input',updateZoneDisplay);"
"document.getElementById('hrResting').addEventListener('input',updateZoneDisplay);"
"function updateZoneDisplay(){"
"const hrMax=parseInt(document.getElementById('hrMax').value)||0;"
"const hrRest=parseInt(document.getElementById('hrResting').value)||0;"
"const reserve=hrMax-hrRest;"
"const zone1=Math.round(hrRest+(0.4*reserve));"
"const zone2=Math.round(0.7*hrMax);"
"const zone3=Math.round(0.8*hrMax);"
"document.getElementById('zone1Display').textContent=zone1;"
"document.getElementById('zone2Display').textContent=zone2;"
"document.getElementById('zone3Display').textContent=zone3;"
"}"
"document.getElementById('configForm').addEventListener('submit',async(e)=>{"
"e.preventDefault();"
"const formData=new FormData(e.target);"
"const data={"
"apSSID:formData.get('apSSID'),"
"apPassword:formData.get('apPassword'),"
"useStationMode:formData.get('useStationMode')?1:0,"
"wifiSSID:formData.get('wifiSSID')||'',"
"wifiPassword:formData.get('wifiPassword')||'',"
"hrMax:parseInt(formData.get('hrMax')),"
"hrResting:parseInt(formData.get('hrResting')),"
"alwaysOn:formData.get('alwaysOn')?1:0,"
"fanDelay:parseInt(formData.get('fanDelay'))*1000,"
"hrHysteresis:parseInt(formData.get('hrHysteresis'))"
"};"
"try{"
"const response=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});"
"const status=document.getElementById('status');"
"if(response.ok){"
"status.className='status success';"
"status.textContent='Configuration saved! Device will restart in 3 seconds...';"
"}else{"
"status.className='status error';"
"status.textContent='Failed to save configuration';"
"}"
"}catch(err){"
"const status=document.getElementById('status');"
"status.className='status error';"
"status.textContent='Error: '+err.message;"
"}"
"});"
"</script>"
"</body>"
"</html>";

// HTTP GET handler for root page
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP GET handler for /api/config
static esp_err_t config_get_handler(httpd_req_t *req)
{
    char json_response[512];
    snprintf(json_response, sizeof(json_response),
             "{\"apSSID\":\"%s\",\"apPassword\":\"%s\",\"useStationMode\":%s,"
             "\"wifiSSID\":\"%s\",\"wifiPassword\":\"%s\","
             "\"hrMax\":%d,\"hrResting\":%d,\"alwaysOn\":%d,"
             "\"fanDelay\":%" PRIu32 ",\"hrHysteresis\":%d}",
             g_config.apSSID,
             g_config.apPassword,
             g_config.useStationMode ? "true" : "false",
             g_config.wifiSSID,
             g_config.wifiPassword,
             g_config.hrMax,
             g_config.hrResting,
             g_config.alwaysOn,
             g_config.fanDelay,
             g_config.hrHysteresis);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_response);
    return ESP_OK;
}

// Simple JSON string extraction helper
static bool extract_json_string(const char *json, const char *key, char *out, size_t out_size)
{
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "\"%s\":\"", key);

    const char *start = strstr(json, search_key);
    if (!start) return false;

    start += strlen(search_key);
    const char *end = strchr(start, '"');
    if (!end) return false;

    size_t len = end - start;
    if (len >= out_size) len = out_size - 1;

    strncpy(out, start, len);
    out[len] = '\0';
    return true;
}

// Simple JSON number extraction helper
static bool extract_json_int(const char *json, const char *key, int *out)
{
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);

    const char *start = strstr(json, search_key);
    if (!start) return false;

    start += strlen(search_key);
    *out = atoi(start);
    return true;
}

// HTTP POST handler for /api/config
static esp_err_t config_post_handler(httpd_req_t *req)
{
    char buf[1024];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    ESP_LOGI(TAG, "Received config: %s", buf);

    // Parse JSON manually
    extract_json_string(buf, "apSSID", g_config.apSSID, sizeof(g_config.apSSID));
    extract_json_string(buf, "apPassword", g_config.apPassword, sizeof(g_config.apPassword));
    extract_json_string(buf, "wifiSSID", g_config.wifiSSID, sizeof(g_config.wifiSSID));
    extract_json_string(buf, "wifiPassword", g_config.wifiPassword, sizeof(g_config.wifiPassword));

    int temp_val;
    if (extract_json_int(buf, "useStationMode", &temp_val)) {
        g_config.useStationMode = (temp_val != 0);
    }
    if (extract_json_int(buf, "hrMax", &temp_val)) {
        g_config.hrMax = temp_val;
    }
    if (extract_json_int(buf, "hrResting", &temp_val)) {
        g_config.hrResting = temp_val;
    }
    if (extract_json_int(buf, "alwaysOn", &temp_val)) {
        g_config.alwaysOn = temp_val;
    }
    if (extract_json_int(buf, "fanDelay", &temp_val)) {
        g_config.fanDelay = temp_val;
    }
    if (extract_json_int(buf, "hrHysteresis", &temp_val)) {
        g_config.hrHysteresis = temp_val;
    }

    // Save config
    nvs_config_save();

    // Send response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");

    // Restart after delay
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return ESP_OK;
}

static const httpd_uri_t root_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t config_get_uri = {
    .uri       = "/api/config",
    .method    = HTTP_GET,
    .handler   = config_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t config_post_uri = {
    .uri       = "/api/config",
    .method    = HTTP_POST,
    .handler   = config_post_handler,
    .user_ctx  = NULL
};

void web_server_init(void)
{
    ESP_LOGI(TAG, "Initializing web server");
}

void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &config_get_uri);
        httpd_register_uri_handler(server, &config_post_uri);
        ESP_LOGI(TAG, "Web server started successfully");
    } else {
        ESP_LOGE(TAG, "Error starting web server!");
    }
}
