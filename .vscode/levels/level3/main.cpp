/*
Level 3: WiFi + Camera + Web Server
===================================

Description:
-----------
1. WiFi Auto-Connect (Level 1):
   - Multiple WiFi networks scan
   - Auto-connect with predefined passwords
   - Connection monitoring

2. Camera Features (Level 2):
   - YUV422 format with 640x480 resolution
   - Auto image quality optimization
   - Flash LED control
   - Automatic photo capture

3. Web Server Features (New in Level 3):
   - Live camera stream in browser
   - Camera control panel:
     * Capture photo button
     * Flash LED toggle
     * Brightness/Contrast controls
     * Resolution changer
   - Last captured photo display
   - System status monitoring

Hardware Connections:
------------------
Same as Level 2

Usage Instructions:
----------------
1. Upload code and connect ESP32-CAM
2. Check serial monitor for IP address
3. Open browser and enter IP address
4. Use web interface to:
   - View live stream
   - Capture photos
   - Control camera settings
   - Monitor system status

Author: Your Name
Date: Current Date
*/

#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Pin Definitions
const int FLASH_LED = 4;    // GPIO 4 is connected to Flash LED
const int LED_BUILTIN = 33; // GPIO 33 is built-in LED

// ... Previous level's constants and pin definitions ...

// Web server instance
httpd_handle_t camera_httpd = NULL;

// JPEG streaming ke liye variables
uint8_t *_jpg_buf = NULL;
size_t _jpg_buf_len = 0;

// Stream boundary markers
static const char *_STREAM_BOUNDARY = "\r\n--123456789000000000000987654321\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// HTML page ka template
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32-CAM Web Server</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; text-align: center; margin: 0px auto; }
        .button {
            background-color: #4CAF50;
            border: none;
            color: white;
            padding: 10px 20px;
            text-align: center;
            font-size: 16px;
            margin: 4px 2px;
            cursor: pointer;
        }
        .slider { width: 200px; }
        img { width: auto; max-width: 100%; height: auto; }
    </style>
</head>
<body>
    <h1>ESP32-CAM Web Control</h1>
    <img src="" id="photo" >
    <div>
        <button class="button" onclick="toggleStream()">Start/Stop Stream</button>
        <button class="button" onclick="capturePhoto()">Capture Photo</button>
        <button class="button" onclick="toggleFlash()">Toggle Flash</button>
    </div>
    <div>
        <h3>Camera Settings</h3>
        <p>Brightness: <input type="range" class="slider" id="brightness" min="-2" max="2" value="0" onchange="updateCamera(this)"></p>
        <p>Contrast: <input type="range" class="slider" id="contrast" min="-2" max="2" value="0" onchange="updateCamera(this)"></p>
    </div>
    <div>
        <h3>System Status</h3>
        <p id="status">Connecting...</p>
    </div>
    <script>
        var streaming = false;
        var baseHost = document.location.origin;
        var streamUrl = baseHost + '/stream';
        
        function toggleStream() {
            if (streaming) {
                document.getElementById('photo').src = "";
                streaming = false;
            } else {
                document.getElementById('photo').src = streamUrl;
                streaming = true;
            }
        }
        
        function capturePhoto() {
            fetch(baseHost + '/capture')
                .then(response => response.blob())
                .then(blob => {
                    document.getElementById('photo').src = URL.createObjectURL(blob);
                });
        }
        
        function toggleFlash() {
            fetch(baseHost + '/flash');
        }
        
        function updateCamera(element) {
            fetch(baseHost + '/control?' + element.id + '=' + element.value);
        }
        
        // Status update every 5 seconds
        setInterval(function() {
            fetch(baseHost + '/status')
                .then(response => response.text())
                .then(text => {
                    document.getElementById('status').innerHTML = text;
                });
        }, 5000);
    </script>
</body>
</html>
)rawliteral";

// Web server ke endpoints handle karne ke functions
esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html, strlen(index_html));
}

esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char *part_buf[64];
    static int64_t last_frame = 0;

    res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=123456789000000000000987654321");
    if (res != ESP_OK)
        return res;

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            Serial.println("Camera frame capture failed");
            res = ESP_FAIL;
        }
        else
        {
            if (fb->format != PIXFORMAT_JPEG)
            {
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                if (!jpeg_converted)
                {
                    Serial.println("JPEG compression failed");
                    res = ESP_FAIL;
                }
            }
            else
            {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }

        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }

        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        }

        if (res != ESP_OK)
            break;
    }

    return res;
}

esp_err_t cmd_handler(httpd_req_t *req)
{
    char *buf;
    size_t buf_len;
    char variable[32] = {
        0,
    };
    char value[32] = {
        0,
    };

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = (char *)malloc(buf_len);
        if (!buf)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
                httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK)
            {
            }
            else
            {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        }
        else
        {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    }
    else
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    sensor_t *s = esp_camera_sensor_get();
    int val = atoi(value);

    if (!strcmp(variable, "framesize"))
    {
        if (s->pixformat == PIXFORMAT_JPEG)
            s->set_framesize(s, (framesize_t)val);
    }
    else if (!strcmp(variable, "quality"))
        s->set_quality(s, val);
    else if (!strcmp(variable, "contrast"))
        s->set_contrast(s, val);
    else if (!strcmp(variable, "brightness"))
        s->set_brightness(s, val);
    else if (!strcmp(variable, "saturation"))
        s->set_saturation(s, val);
    else if (!strcmp(variable, "flash"))
    {
        digitalWrite(FLASH_LED, !digitalRead(FLASH_LED));
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

// Web server start karne ka function
void startWebServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};

    httpd_uri_t cmd_uri = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL};

    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &stream_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        Serial.println("Web server successfully started");
    }
}

// ... rest of the previous level's code ...