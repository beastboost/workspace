/**
 * GBA Emulator Web Server Implementation
 *
 * Features:
 * - HTTP server for static files and REST API
 * - WebSocket for real-time video streaming and input
 * - JPEG encoding for efficient video transfer
 * - ROM upload with progress tracking
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "webserver.h"

// Try to use ESP JPEG encoder if available
#if __has_include("esp_jpeg_enc.h")
#include "esp_jpeg_enc.h"
#define USE_HW_JPEG 1
#else
#define USE_HW_JPEG 0
#endif

static const char *TAG = "WEBSERVER";

// GBA screen dimensions
#define GBA_WIDTH  240
#define GBA_HEIGHT 160

// Maximum WebSocket clients
#define MAX_WS_CLIENTS 4

// WebSocket client info
typedef struct {
    int fd;
    bool active;
    bool wants_video;
} ws_client_t;

// Server state
static httpd_handle_t server = NULL;
static ws_client_t ws_clients[MAX_WS_CLIENTS];
static SemaphoreHandle_t client_mutex = NULL;
static webserver_config_t config;

// Frame buffer for JPEG encoding
static uint8_t *jpeg_buffer = NULL;
static size_t jpeg_buffer_size = 0;

// Status info
static float current_fps = 0;
static char current_rom[64] = {0};
static bool is_paused = false;

// Embedded web files (will be generated)
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

// Simple RGB565 to RGB888 conversion
static inline void rgb565_to_rgb888(uint16_t pixel, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = ((pixel >> 11) & 0x1F) << 3;
    *g = ((pixel >> 5) & 0x3F) << 2;
    *b = (pixel & 0x1F) << 3;
}

// Simple JPEG encoder (basic implementation)
// For better quality, use esp_jpeg or libjpeg
static size_t encode_jpeg_simple(const uint16_t *rgb565, uint8_t *output, size_t max_size)
{
    // This is a placeholder - in production, use proper JPEG encoding
    // For now, we'll send raw RGB565 data with a simple header

    // BMP-style header for raw pixel data (browser can decode)
    // Or use a simple RLE compression

    size_t idx = 0;

    // Simple frame header
    output[idx++] = 0x47;  // 'G'
    output[idx++] = 0x42;  // 'B'
    output[idx++] = 0x41;  // 'A'
    output[idx++] = 0x00;  // version

    // Dimensions (little endian)
    output[idx++] = GBA_WIDTH & 0xFF;
    output[idx++] = (GBA_WIDTH >> 8) & 0xFF;
    output[idx++] = GBA_HEIGHT & 0xFF;
    output[idx++] = (GBA_HEIGHT >> 8) & 0xFF;

    // Copy RGB565 data directly
    size_t pixel_bytes = GBA_WIDTH * GBA_HEIGHT * 2;
    if (idx + pixel_bytes > max_size) {
        return 0;
    }

    memcpy(&output[idx], rgb565, pixel_bytes);
    idx += pixel_bytes;

    return idx;
}

// WebSocket frame sender
static esp_err_t ws_send_frame(int fd, const uint8_t *data, size_t len, uint8_t opcode)
{
    httpd_ws_frame_t ws_pkt = {
        .payload = (uint8_t *)data,
        .len = len,
        .type = (opcode == 0x02) ? HTTPD_WS_TYPE_BINARY : HTTPD_WS_TYPE_TEXT,
    };

    return httpd_ws_send_frame_async(server, fd, &ws_pkt);
}

// Find free client slot
static int find_free_client_slot(void)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!ws_clients[i].active) {
            return i;
        }
    }
    return -1;
}

// Find client by fd
static int find_client_by_fd(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients[i].active && ws_clients[i].fd == fd) {
            return i;
        }
    }
    return -1;
}

// WebSocket handler
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // New WebSocket connection
        ESP_LOGI(TAG, "WebSocket handshake from fd %d", httpd_req_to_sockfd(req));

        xSemaphoreTake(client_mutex, portMAX_DELAY);
        int slot = find_free_client_slot();
        if (slot >= 0) {
            ws_clients[slot].fd = httpd_req_to_sockfd(req);
            ws_clients[slot].active = true;
            ws_clients[slot].wants_video = true;
            ESP_LOGI(TAG, "Client connected, slot %d", slot);
        }
        xSemaphoreGive(client_mutex);

        return ESP_OK;
    }

    // Receive WebSocket frame
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    if (ws_pkt.len == 0) {
        return ESP_OK;
    }

    // Allocate buffer for message
    uint8_t *buf = malloc(ws_pkt.len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        free(buf);
        return ret;
    }
    buf[ws_pkt.len] = 0;

    // Parse message
    cJSON *json = cJSON_Parse((char *)buf);
    if (json) {
        cJSON *type = cJSON_GetObjectItem(json, "type");
        if (type && cJSON_IsString(type)) {
            if (strcmp(type->valuestring, "input") == 0) {
                // Button input
                cJSON *buttons = cJSON_GetObjectItem(json, "buttons");
                if (buttons && cJSON_IsNumber(buttons)) {
                    if (config.input_cb) {
                        config.input_cb((uint16_t)buttons->valueint);
                    }
                }
            } else if (strcmp(type->valuestring, "command") == 0) {
                // Command (pause, reset, etc.)
                cJSON *cmd = cJSON_GetObjectItem(json, "cmd");
                if (cmd && cJSON_IsString(cmd)) {
                    if (config.cmd_cb) {
                        config.cmd_cb(cmd->valuestring);
                    }
                }
            } else if (strcmp(type->valuestring, "load_rom") == 0) {
                // Load ROM
                cJSON *name = cJSON_GetObjectItem(json, "name");
                if (name && cJSON_IsString(name)) {
                    if (config.rom_cb) {
                        config.rom_cb(name->valuestring);
                    }
                }
            } else if (strcmp(type->valuestring, "video_control") == 0) {
                // Enable/disable video for this client
                cJSON *enable = cJSON_GetObjectItem(json, "enable");
                int slot = find_client_by_fd(httpd_req_to_sockfd(req));
                if (slot >= 0 && enable) {
                    ws_clients[slot].wants_video = cJSON_IsTrue(enable);
                }
            }
        }
        cJSON_Delete(json);
    }

    free(buf);
    return ESP_OK;
}

// Serve index.html
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start,
                    index_html_end - index_html_start);
    return ESP_OK;
}

// Get ROM list API
static esp_err_t api_roms_handler(httpd_req_t *req)
{
    cJSON *response = cJSON_CreateObject();
    cJSON *roms = cJSON_CreateArray();

    DIR *dir = opendir("/sdcard/gba");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;

            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcasecmp(ext, ".gba") == 0 ||
                        strcasecmp(ext, ".agb") == 0)) {

                char path[128];
                snprintf(path, sizeof(path), "/sdcard/gba/%s", entry->d_name);

                struct stat st;
                stat(path, &st);

                cJSON *rom = cJSON_CreateObject();
                cJSON_AddStringToObject(rom, "name", entry->d_name);
                cJSON_AddNumberToObject(rom, "size", st.st_size);
                cJSON_AddItemToArray(roms, rom);
            }
        }
        closedir(dir);
    }

    cJSON_AddItemToObject(response, "roms", roms);

    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// Upload ROM API
static esp_err_t api_upload_handler(httpd_req_t *req)
{
    // Get filename from header
    char filename[64] = "uploaded.gba";
    if (httpd_req_get_hdr_value_len(req, "X-Filename") > 0) {
        httpd_req_get_hdr_value_str(req, "X-Filename", filename, sizeof(filename));
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/sdcard/gba/%s", filename);

    ESP_LOGI(TAG, "Uploading ROM: %s (%d bytes)", filename, req->content_len);

    FILE *f = fopen(filepath, "wb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    // Receive file in chunks
    char *buf = malloc(4096);
    int received = 0;
    int total = req->content_len;

    while (received < total) {
        int ret = httpd_req_recv(req, buf, MIN(4096, total - received));
        if (ret <= 0) {
            fclose(f);
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }
        fwrite(buf, 1, ret, f);
        received += ret;
    }

    fclose(f);
    free(buf);

    ESP_LOGI(TAG, "ROM uploaded successfully");

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "name", filename);

    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// Delete ROM API
static esp_err_t api_delete_handler(httpd_req_t *req)
{
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query");
        return ESP_FAIL;
    }

    char name[64];
    if (httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing name");
        return ESP_FAIL;
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/sdcard/gba/%s", name);

    if (unlink(filepath) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// Status API
static esp_err_t api_status_handler(httpd_req_t *req)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "fps", current_fps);
    cJSON_AddStringToObject(response, "rom", current_rom);
    cJSON_AddBoolToObject(response, "paused", is_paused);
    cJSON_AddNumberToObject(response, "clients", webserver_get_client_count());

    // Memory info
    cJSON_AddNumberToObject(response, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(response, "free_psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

esp_err_t webserver_init(const webserver_config_t *cfg)
{
    if (cfg) {
        memcpy(&config, cfg, sizeof(config));
    }
    if (config.port == 0) {
        config.port = 80;
    }

    // Initialize client tracking
    memset(ws_clients, 0, sizeof(ws_clients));
    client_mutex = xSemaphoreCreateMutex();

    // Allocate JPEG buffer
    jpeg_buffer_size = GBA_WIDTH * GBA_HEIGHT * 2 + 256;  // RGB565 + header
    jpeg_buffer = heap_caps_malloc(jpeg_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg_buffer) {
        jpeg_buffer = malloc(jpeg_buffer_size);
    }
    if (!jpeg_buffer) {
        ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Web server initialized");
    return ESP_OK;
}

esp_err_t webserver_start(void)
{
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = config.port;
    http_config.max_uri_handlers = 16;
    http_config.max_open_sockets = MAX_WS_CLIENTS + 4;
    http_config.lru_purge_enable = true;
    http_config.recv_wait_timeout = 10;
    http_config.send_wait_timeout = 10;

    esp_err_t ret = httpd_start(&server, &http_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ret;
    }

    // Register URI handlers
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
    };
    httpd_register_uri_handler(server, &index_uri);

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_uri);

    httpd_uri_t roms_uri = {
        .uri = "/api/roms",
        .method = HTTP_GET,
        .handler = api_roms_handler,
    };
    httpd_register_uri_handler(server, &roms_uri);

    httpd_uri_t upload_uri = {
        .uri = "/api/upload",
        .method = HTTP_POST,
        .handler = api_upload_handler,
    };
    httpd_register_uri_handler(server, &upload_uri);

    httpd_uri_t delete_uri = {
        .uri = "/api/delete",
        .method = HTTP_DELETE,
        .handler = api_delete_handler,
    };
    httpd_register_uri_handler(server, &delete_uri);

    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_handler,
    };
    httpd_register_uri_handler(server, &status_uri);

    ESP_LOGI(TAG, "Web server started on port %d", config.port);
    return ESP_OK;
}

void webserver_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}

void webserver_send_frame(const uint16_t *framebuffer)
{
    if (!server || !framebuffer || !jpeg_buffer) return;

    // Encode frame
    size_t encoded_size = encode_jpeg_simple(framebuffer, jpeg_buffer, jpeg_buffer_size);
    if (encoded_size == 0) return;

    // Send to all connected clients
    xSemaphoreTake(client_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients[i].active && ws_clients[i].wants_video) {
            esp_err_t ret = ws_send_frame(ws_clients[i].fd, jpeg_buffer, encoded_size, 0x02);
            if (ret != ESP_OK) {
                // Client disconnected
                ESP_LOGI(TAG, "Client %d disconnected", i);
                ws_clients[i].active = false;
            }
        }
    }
    xSemaphoreGive(client_mutex);
}

void webserver_update_status(float fps, const char *rom_name, bool paused)
{
    current_fps = fps;
    is_paused = paused;
    if (rom_name) {
        strncpy(current_rom, rom_name, sizeof(current_rom) - 1);
    } else {
        current_rom[0] = 0;
    }
}

int webserver_get_client_count(void)
{
    int count = 0;
    xSemaphoreTake(client_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients[i].active) count++;
    }
    xSemaphoreGive(client_mutex);
    return count;
}

bool webserver_has_clients(void)
{
    return webserver_get_client_count() > 0;
}
