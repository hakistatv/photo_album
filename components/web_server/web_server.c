#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "epd.h"
#include "jpeg_display.h"
#include "photo_store.h"
#include "device_settings.h"
#include "build_info.h"
#include "web_server.h"

static const char *TAG = "web_server";

/* HTML pages (and the shared style.css) live under pages/ and are embedded
 * into the binary at build time (EMBED_TXTFILES in CMakeLists.txt),
 * null-terminated, so they can be used directly as C strings. home.html and
 * restart.html need no substitution and are sent as-is. upload.html and
 * settings.html are printf-style templates -- see upload_get_handler and
 * settings_get_handler for what each "%s" slot means. */
extern const char home_html_start[] asm("_binary_home_html_start");
extern const char upload_html_start[] asm("_binary_upload_html_start");
extern const char settings_html_start[] asm("_binary_settings_html_start");
extern const char restart_html_start[] asm("_binary_restart_html_start");
extern const char style_css_start[] asm("_binary_style_css_start");

/* A 200x200 JPEG is a genuinely small image -- even at very high quality
 * it's well under this. Kept deliberately tight (rather than a large round
 * number) because the decode pipeline briefly holds this upload buffer
 * alongside the ~120KB RGB888 decode buffer (grayscale conversion happens
 * in place, reusing that same buffer -- see jpeg_display.c), and this
 * device's internal RAM has to cover that plus Wi-Fi/httpd/mDNS overhead
 * all at once. */
#define MAX_UPLOAD_LEN (64 * 1024)

_Static_assert(PHOTO_STORE_NUM_SLOTS == 5, "the slot_uri table below is hand-written for 5 slots");

/* Decodes an application/x-www-form-urlencoded value in place: '+' -> space,
 * '%XX' -> byte XX. esp_http_server's httpd_query_key_value() only splits
 * out the raw substring; it doesn't do this decoding for you. */
static void url_decode(char *str) {
    char *src = str;
    char *dst = str;
    while (*src) {
        if (src[0] == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], 0 };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* Escapes a string for safe use inside an HTML attribute value. Truncates
 * (rather than overflows) if dst_size is too small for the escaped result. */
static void html_escape(const char *src, char *dst, size_t dst_size) {
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0'; si++) {
        const char *rep = NULL;
        switch (src[si]) {
            case '&':  rep = "&amp;";  break;
            case '<':  rep = "&lt;";   break;
            case '>':  rep = "&gt;";   break;
            case '"':  rep = "&quot;"; break;
            default: break;
        }
        size_t add_len = rep ? strlen(rep) : 1;
        if (di + add_len >= dst_size) {
            break;
        }
        if (rep) {
            memcpy(dst + di, rep, add_len);
        } else {
            dst[di] = src[si];
        }
        di += add_len;
    }
    dst[di] = '\0';
}

/* Naive byte-string search (can't use strstr()/memmem() here -- the JPEG
 * payload is binary and may contain embedded NUL bytes, and memmem() isn't
 * guaranteed available in ESP-IDF's libc). haystack_len/needle_len are
 * small enough (tens of KB / tens of bytes) that this is effectively
 * instant on this chip. */
static const uint8_t *find_bytes(const uint8_t *haystack, size_t haystack_len,
                                  const uint8_t *needle, size_t needle_len) {
    if (needle_len == 0 || haystack_len < needle_len) {
        return NULL;
    }
    for (size_t i = 0; i + needle_len <= haystack_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

/* Finds the multipart/form-data part named `field_name` (matched against
 * its Content-Disposition "name=" attribute) and returns a pointer to (and
 * length of) its raw content, stripped of the surrounding envelope
 * (headers, and the trailing CRLF before the next boundary). Returns NULL
 * if the field isn't present or the body is malformed. */
static const uint8_t *find_multipart_field(const uint8_t *body, size_t body_len,
                                            const uint8_t *delimiter, size_t delimiter_len,
                                            const char *field_name, size_t *out_len) {
    const uint8_t *end = body + body_len;
    const uint8_t *cursor = body;

    while (cursor < end) {
        const uint8_t *part_start = find_bytes(cursor, (size_t)(end - cursor), delimiter, delimiter_len);
        if (!part_start) {
            return NULL;
        }
        part_start += delimiter_len;
        if (part_start + 2 <= end && part_start[0] == '-' && part_start[1] == '-') {
            return NULL; /* "--" right after the boundary marks the final one -- no more parts */
        }

        const uint8_t *headers_end = find_bytes(part_start, (size_t)(end - part_start),
                                                  (const uint8_t *)"\r\n\r\n", 4);
        if (!headers_end) {
            return NULL;
        }
        size_t headers_len = (size_t)(headers_end - part_start);
        const uint8_t *data_start = headers_end + 4;

        const uint8_t *next_delim = find_bytes(data_start, (size_t)(end - data_start), delimiter, delimiter_len);
        if (!next_delim) {
            return NULL; /* malformed -- no closing boundary */
        }
        size_t data_len = (size_t)(next_delim - data_start);
        if (data_len >= 2 && data_start[data_len - 2] == '\r' && data_start[data_len - 1] == '\n') {
            data_len -= 2;
        }

        char name_needle[64];
        int n = snprintf(name_needle, sizeof(name_needle), "name=\"%s\"", field_name);
        if (n > 0 && (size_t)n < sizeof(name_needle)
            && find_bytes(part_start, headers_len, (const uint8_t *)name_needle, (size_t)n)) {
            *out_len = data_len;
            return data_start;
        }

        cursor = next_delim; /* not a match -- keep scanning from the next boundary */
    }
    return NULL;
}

/* Appends a printf-formatted fragment to `buf`, tracking how much of it is
 * used so callers can build HTML piecemeal (e.g. one <div> per slot) into a
 * fixed-size buffer without manually re-deriving offsets each time. Silently
 * stops writing (rather than overflowing) once `buf` is full. */
static void append(char *buf, size_t buf_size, size_t *used, const char *fmt, ...) {
    if (*used >= buf_size) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *used, buf_size - *used, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *used += (size_t)n;
        if (*used > buf_size) {
            *used = buf_size;
        }
    }
}

/* Builds the "Slots" preview grid: an <img> (served by slot_get_handler)
 * for each occupied slot, or an "Empty" placeholder otherwise. */
static void build_slot_grid(char *buf, size_t buf_size) {
    size_t used = 0;
    for (int slot = 1; slot <= PHOTO_STORE_NUM_SLOTS; slot++) {
        if (photo_store_is_occupied(slot)) {
            append(buf, buf_size, &used,
                   "<div class=\"slot\"><span class=\"slot-label\">Slot %d</span>"
                   "<img src=\"/slot/%d\" width=\"100\" height=\"100\" alt=\"Slot %d\">"
                   "<form method=\"POST\" action=\"/slot/%d/clear\" class=\"clear-form\">"
                   "<button type=\"submit\" class=\"clear-btn\">Clear</button></form></div>",
                   slot, slot, slot, slot);
        } else {
            append(buf, buf_size, &used,
                   "<div class=\"slot\"><span class=\"slot-label\">Slot %d</span>"
                   "<p class=\"empty\">Empty</p></div>",
                   slot);
        }
    }
}

/* Builds the "save to slot" radio-button picker used in the upload form. */
static void build_slot_picker(char *buf, size_t buf_size) {
    size_t used = 0;
    for (int slot = 1; slot <= PHOTO_STORE_NUM_SLOTS; slot++) {
        append(buf, buf_size, &used,
               "<label class=\"slot-option\"><input type=\"radio\" name=\"slot\" value=\"%d\" required> "
               "Slot %d</label>",
               slot, slot);
    }
}

/* --- 1-bit BMP encoding, for previewing a stored slot in a browser --- */

#define BMP_ROW_STRIDE  ((size_t)((EPD_WIDTH + 31) / 32 * 4)) /* row bytes, padded to a 4-byte boundary */
#define BMP_HEADER_SIZE ((size_t)62)                          /* 14-byte file header + 40-byte info header + 8-byte palette */
#define BMP_FILE_SIZE   (BMP_HEADER_SIZE + BMP_ROW_STRIDE * EPD_HEIGHT)

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* Wraps a photo_store framebuffer (EPD_BUFFER_SIZE bytes) as a standalone
 * 1-bit BMP file (`out` must be at least BMP_FILE_SIZE bytes). Stored
 * top-down (negative height) so framebuffer rows can be copied straight
 * across without reversing row order. Palette is index 0 = black, index 1 =
 * white, matching this project's framebuffer bit convention exactly (0 =
 * black, 1 = white -- see epd_draw_pixel()), so pixel bytes need no
 * transformation, just padding out to the BMP row stride. */
static void build_bmp(const uint8_t *framebuffer, uint8_t *out) {
    memset(out, 0, BMP_HEADER_SIZE);

    out[0] = 'B';
    out[1] = 'M';
    put_u32(out + 2, (uint32_t)BMP_FILE_SIZE);
    put_u32(out + 10, (uint32_t)BMP_HEADER_SIZE); /* pixel data offset */

    put_u32(out + 14, 40); /* BITMAPINFOHEADER size */
    put_u32(out + 18, EPD_WIDTH);
    put_u32(out + 22, (uint32_t)(-(int32_t)EPD_HEIGHT)); /* negative = top-down */
    put_u16(out + 26, 1);                                /* planes */
    put_u16(out + 28, 1);                                /* bits per pixel */
    put_u32(out + 34, (uint32_t)(BMP_ROW_STRIDE * EPD_HEIGHT));

    put_u32(out + 54, 0x00000000u); /* palette[0] = black */
    put_u32(out + 58, 0x00FFFFFFu); /* palette[1] = white (0x00 BB GG RR) */

    uint8_t *row_dst = out + BMP_HEADER_SIZE;
    const size_t row_src_bytes = EPD_WIDTH / 8;
    for (int y = 0; y < EPD_HEIGHT; y++) {
        memset(row_dst, 0, BMP_ROW_STRIDE);
        memcpy(row_dst, framebuffer + (size_t)y * row_src_bytes, row_src_bytes);
        row_dst += BMP_ROW_STRIDE;
    }
}

/* Shared stylesheet -- one static file, so the browser caches it after the
 * first page load. */
static esp_err_t style_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/css");
    return httpd_resp_send(req, style_css_start, HTTPD_RESP_USE_STRLEN);
}

/* GET / -- Home: just links to Upload Image and Settings. No template
 * substitution needed, so the embedded page is sent as-is. */
static esp_err_t home_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, home_html_start, HTTPD_RESP_USE_STRLEN);
}

/* GET /upload -- shows the slot previews and the upload form. After a
 * successful upload, the POST handler redirects here with "?updated=1" so a
 * one-time confirmation banner can be shown (and a plain page reload
 * doesn't keep re-showing it). */
static esp_err_t upload_get_handler(httpd_req_t *req) {
    bool updated = false;
    size_t qs_len = httpd_req_get_url_query_len(req);
    if (qs_len > 0 && qs_len < 64) {
        char qs[64];
        if (httpd_req_get_url_query_str(req, qs, sizeof(qs)) == ESP_OK) {
            char val[4];
            updated = httpd_query_key_value(qs, "updated", val, sizeof(val)) == ESP_OK;
        }
    }

    char slot_grid[1536];
    build_slot_grid(slot_grid, sizeof(slot_grid));

    char slot_picker[768];
    build_slot_picker(slot_picker, sizeof(slot_picker));

    char *page = malloc(4096);
    if (!page) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int len = snprintf(page, 4096, upload_html_start,
                        updated ? "<p class=\"status\">Image displayed.</p>" : "",
                        slot_grid, slot_picker);
    if (len < 0 || (size_t)len >= 4096) {
        free(page);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Page too large");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    esp_err_t err = httpd_resp_send(req, page, len);
    free(page);
    return err;
}

/* GET /slot/<n> -- serves the stored image in slot n as a BMP, for the
 * preview grid's <img> tags. 404s if the slot is empty. */
static esp_err_t slot_get_handler(httpd_req_t *req) {
    int slot = (int)(intptr_t)req->user_ctx;

    uint8_t *framebuffer = malloc(EPD_BUFFER_SIZE);
    if (!framebuffer) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    if (!photo_store_load(slot, framebuffer, EPD_BUFFER_SIZE)) {
        free(framebuffer);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Slot is empty");
        return ESP_FAIL;
    }

    uint8_t *bmp = malloc(BMP_FILE_SIZE);
    if (!bmp) {
        free(framebuffer);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    build_bmp(framebuffer, bmp);
    free(framebuffer);

    httpd_resp_set_type(req, "image/bmp");
    esp_err_t err = httpd_resp_send(req, (const char *)bmp, BMP_FILE_SIZE);
    free(bmp);
    return err;
}

/* POST /slot/<n>/clear -- removes the stored image from slot n (does not
 * touch whatever's currently drawn on the panel). Redirects back to
 * GET /upload. */
static esp_err_t slot_clear_handler(httpd_req_t *req) {
    int slot = (int)(intptr_t)req->user_ctx;

    if (!photo_store_clear(slot)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to clear slot -- check the device log");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Cleared slot %d", slot);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/upload");
    return httpd_resp_send(req, NULL, 0);
}

/* POST /upload -- accepts a multipart/form-data upload with two fields:
 * "slot" (which of the PHOTO_STORE_NUM_SLOTS slots to save into) and
 * "image" (a 200x200 JPEG). Renders the image to the panel, persists it
 * into the chosen slot, and redirects back to GET /upload
 * (POST/redirect/GET) so a page reload doesn't resubmit the upload. */
static esp_err_t upload_post_handler(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len > MAX_UPLOAD_LEN) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Upload missing or too large");
        return ESP_FAIL;
    }

    char content_type[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK
        || strncmp(content_type, "multipart/form-data", strlen("multipart/form-data")) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected a multipart/form-data upload");
        return ESP_FAIL;
    }

    const char *boundary_param = strstr(content_type, "boundary=");
    if (!boundary_param) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing multipart boundary");
        return ESP_FAIL;
    }
    boundary_param += strlen("boundary=");
    size_t boundary_len = strcspn(boundary_param, "; \r\n");

    char delimiter[128];
    if (boundary_len + 2 >= sizeof(delimiter)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Multipart boundary too long");
        return ESP_FAIL;
    }
    delimiter[0] = '-';
    delimiter[1] = '-';
    memcpy(delimiter + 2, boundary_param, boundary_len);
    size_t delimiter_len = boundary_len + 2;

    uint8_t *body = malloc((size_t)req->content_len);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, (char *)body + received, req->content_len - received);
        if (ret <= 0) {
            free(body);
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        received += ret;
    }

    size_t slot_field_len = 0;
    const uint8_t *slot_field = find_multipart_field(body, (size_t)received,
                                                       (const uint8_t *)delimiter, delimiter_len,
                                                       "slot", &slot_field_len);
    int slot = (slot_field && slot_field_len == 1) ? (slot_field[0] - '0') : -1;
    if (slot < 1 || slot > PHOTO_STORE_NUM_SLOTS) {
        free(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid slot selection");
        return ESP_FAIL;
    }

    size_t image_len = 0;
    const uint8_t *image_data = find_multipart_field(body, (size_t)received,
                                                       (const uint8_t *)delimiter, delimiter_len,
                                                       "image", &image_len);
    if (!image_data || image_len == 0) {
        free(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing image file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Decoding upload for slot %d (%u bytes), free heap: %u bytes",
             slot, (unsigned)image_len, (unsigned)esp_get_free_heap_size());

    /* Locked from here through epd_display() -- the boot button's
     * slot-cycle handler (see photo_album.c) also touches the framebuffer
     * and panel from its own task, and the two must not interleave. */
    epd_lock();
    bool ok = jpeg_display_render(image_data, image_len);
    if (!ok) {
        epd_unlock();
        free(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                             "Not a valid 200x200 JPEG image -- check the device log for the exact reason");
        return ESP_FAIL;
    }
    free(body); /* image_data no longer needed -- the panel framebuffer already has the decoded image */

    if (!photo_store_save(slot, epd_get_framebuffer(), EPD_BUFFER_SIZE)) {
        epd_unlock();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                             "Decoded OK but failed to save to the slot -- check the device log");
        return ESP_FAIL;
    }
    epd_display();
    epd_unlock();

    ESP_LOGI(TAG, "Image displayed and saved to slot %d", slot);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/upload?updated=1");
    return httpd_resp_send(req, NULL, 0);
}

/* GET /settings -- shows the current Wi-Fi SSID/hostname (password is left
 * blank rather than echoing the current one back into the page source). */
static esp_err_t settings_get_handler(httpd_req_t *req) {
    char ssid_esc[SSID_MAX_LEN * 6 + 1];
    char host_esc[HOST_MAX_LEN * 6 + 1];
    html_escape(current_ssid, ssid_esc, sizeof(ssid_esc));
    html_escape(current_hostname, host_esc, sizeof(host_esc));

    /* Worst case: template (~700 bytes) + ssid_esc/host_esc (up to
     * SSID_MAX_LEN/HOST_MAX_LEN * 6 + 1 = 193 each, if every character
     * needs HTML-escaping) -- comfortably under 1536. */
    char page[1536];
    int len = snprintf(page, sizeof(page), settings_html_start, ssid_esc, host_esc);
    if (len < 0 || (size_t)len >= sizeof(page)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Page too large");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, len);
}

/* Restarting from inside the HTTP handler that sent the response would race
 * the socket close, so hand it to a task that waits for the response to
 * flush first. A restart (rather than a live Wi-Fi reconfigure) is the
 * simplest way to reliably apply a new SSID/password -- matches how
 * ../listening_device's /settings endpoint does it. */
static void restart_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

/* POST /settings -- saves a new Wi-Fi SSID/password/hostname to NVS (via
 * device_settings.h) and restarts the device so they take effect. */
static esp_err_t settings_post_handler(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len >= 256) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Form data missing or too large");
        return ESP_FAIL;
    }

    char body[256];
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        received += ret;
    }
    body[received] = '\0';

    char ssid_val[SSID_MAX_LEN + 1] = {0};
    char pass_val[PASS_MAX_LEN + 1] = {0};
    char host_val[HOST_MAX_LEN + 1] = {0};

    bool has_ssid = httpd_query_key_value(body, "ssid", ssid_val, sizeof(ssid_val)) == ESP_OK;
    bool has_pass = httpd_query_key_value(body, "password", pass_val, sizeof(pass_val)) == ESP_OK;
    bool has_host = httpd_query_key_value(body, "hostname", host_val, sizeof(host_val)) == ESP_OK;
    url_decode(ssid_val);
    url_decode(pass_val);
    url_decode(host_val);

    if (!has_ssid || strlen(ssid_val) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID is required");
        return ESP_FAIL;
    }
    if (!has_host || strlen(host_val) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Device name is required");
        return ESP_FAIL;
    }
    if (has_pass && strlen(pass_val) > 0 && strlen(pass_val) < 8) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                             "Password must be at least 8 characters, or left blank to keep the current one");
        return ESP_FAIL;
    }

    const char *new_pass = (has_pass && strlen(pass_val) > 0) ? pass_val : current_pass;
    esp_err_t err = save_runtime_config(ssid_val, new_pass, host_val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save settings");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, restart_html_start, HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Settings saved (SSID=\"%s\" hostname=\"%s\") -- restarting", ssid_val, host_val);
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* GET /origin -- firmware provenance. Deliberately not linked from any
 * page (Home, Upload, Settings) so it doesn't show up in normal browsing --
 * see build_info.h for why this exists and what it's for. */
static esp_err_t origin_get_handler(httpd_req_t *req) {
    char body[256];
    int len = snprintf(body, sizeof(body),
                        "%s -- photo_album firmware\n"
                        "Origin: %s\n"
                        "YouTube: %s\n"
                        "Mark: %s\n",
                        FW_ORIGIN_AUTHOR, FW_ORIGIN_REPO, FW_ORIGIN_YT, FW_ORIGIN_MARK);
    if (len < 0 || (size_t)len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Buffer too small");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, body, len);
}

static const httpd_uri_t home_get_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = home_get_handler,
};

static const httpd_uri_t upload_get_uri = {
    .uri = "/upload",
    .method = HTTP_GET,
    .handler = upload_get_handler,
};

static const httpd_uri_t upload_post_uri = {
    .uri = "/upload",
    .method = HTTP_POST,
    .handler = upload_post_handler,
};

static const httpd_uri_t settings_get_uri = {
    .uri = "/settings",
    .method = HTTP_GET,
    .handler = settings_get_handler,
};

static const httpd_uri_t settings_post_uri = {
    .uri = "/settings",
    .method = HTTP_POST,
    .handler = settings_post_handler,
};

static const httpd_uri_t style_uri = {
    .uri = "/style.css",
    .method = HTTP_GET,
    .handler = style_get_handler,
};

static const httpd_uri_t origin_uri = {
    .uri = "/origin",
    .method = HTTP_GET,
    .handler = origin_get_handler,
};

/* One entry per slot (PHOTO_STORE_NUM_SLOTS == 5, enforced by the
 * _Static_assert above) -- .uri needs to be a string with static lifetime,
 * so these are spelled out rather than built in a loop. */
static const httpd_uri_t slot1_uri = { .uri = "/slot/1", .method = HTTP_GET, .handler = slot_get_handler, .user_ctx = (void *)(intptr_t)1 };
static const httpd_uri_t slot2_uri = { .uri = "/slot/2", .method = HTTP_GET, .handler = slot_get_handler, .user_ctx = (void *)(intptr_t)2 };
static const httpd_uri_t slot3_uri = { .uri = "/slot/3", .method = HTTP_GET, .handler = slot_get_handler, .user_ctx = (void *)(intptr_t)3 };
static const httpd_uri_t slot4_uri = { .uri = "/slot/4", .method = HTTP_GET, .handler = slot_get_handler, .user_ctx = (void *)(intptr_t)4 };
static const httpd_uri_t slot5_uri = { .uri = "/slot/5", .method = HTTP_GET, .handler = slot_get_handler, .user_ctx = (void *)(intptr_t)5 };

static const httpd_uri_t slot1_clear_uri = { .uri = "/slot/1/clear", .method = HTTP_POST, .handler = slot_clear_handler, .user_ctx = (void *)(intptr_t)1 };
static const httpd_uri_t slot2_clear_uri = { .uri = "/slot/2/clear", .method = HTTP_POST, .handler = slot_clear_handler, .user_ctx = (void *)(intptr_t)2 };
static const httpd_uri_t slot3_clear_uri = { .uri = "/slot/3/clear", .method = HTTP_POST, .handler = slot_clear_handler, .user_ctx = (void *)(intptr_t)3 };
static const httpd_uri_t slot4_clear_uri = { .uri = "/slot/4/clear", .method = HTTP_POST, .handler = slot_clear_handler, .user_ctx = (void *)(intptr_t)4 };
static const httpd_uri_t slot5_clear_uri = { .uri = "/slot/5/clear", .method = HTTP_POST, .handler = slot_clear_handler, .user_ctx = (void *)(intptr_t)5 };

httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192; /* multipart parsing buffers need more than the 4KB default */
    config.max_uri_handlers = 24; /* home, upload x2, settings x2, style, origin, slot/1..5 x2 (view+clear) = 17; default cap is 8 */

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return NULL;
    }

    httpd_register_uri_handler(server, &home_get_uri);
    httpd_register_uri_handler(server, &upload_get_uri);
    httpd_register_uri_handler(server, &upload_post_uri);
    httpd_register_uri_handler(server, &settings_get_uri);
    httpd_register_uri_handler(server, &settings_post_uri);
    httpd_register_uri_handler(server, &style_uri);
    httpd_register_uri_handler(server, &origin_uri);
    httpd_register_uri_handler(server, &slot1_uri);
    httpd_register_uri_handler(server, &slot2_uri);
    httpd_register_uri_handler(server, &slot3_uri);
    httpd_register_uri_handler(server, &slot4_uri);
    httpd_register_uri_handler(server, &slot5_uri);
    httpd_register_uri_handler(server, &slot1_clear_uri);
    httpd_register_uri_handler(server, &slot2_clear_uri);
    httpd_register_uri_handler(server, &slot3_clear_uri);
    httpd_register_uri_handler(server, &slot4_clear_uri);
    httpd_register_uri_handler(server, &slot5_clear_uri);
    ESP_LOGI(TAG, "Web server started");
    return server;
}
