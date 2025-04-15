/*
Level 2: WiFi Auto-Connect + Camera Capture
Description: Level 1 ke WiFi auto-connect ke saath camera functionality add ki hai.
            Ab ESP32-CAM photos capture kar sakta hai aur serial monitor par status dikhata hai.
Author: Your Name
Date: Current Date
*/

#include <Arduino.h>          // Arduino ke basic functions ke liye
#include <WiFi.h>             // WiFi functions ke liye
#include "esp_camera.h"       // ESP32 Camera ke functions ke liye
#include "soc/soc.h"          // ESP32 brownout ke liye
#include "soc/rtc_cntl_reg.h" // ESP32 brownout ke liye

// Passwords ki list - in passwords se WiFi connect karne ki koshish karenge
const char *passwords[] = {
    "ashish20032300", // Password 2
    "Aa@20032300"     // Password 3
};
const int NUM_PASSWORDS = 3; // Total passwords ki count

// ESP32-CAM ke pins define karte hain
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// ESP32-CAM ka built-in LED pin - isse hum status indicate karenge
const int LED_BUILTIN = 33; // GPIO 33 pin LED ke liye use karenge
const int FLASH_LED = 4;    // Flash LED ka pin

// Camera initialize karne ka function
bool initCamera()
{
    camera_config_t config; // Camera ki configuration

    // Camera ke basic parameters set karte hain
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;

    // Image ke parameters set karte hain
    config.xclk_freq_hz = 20000000;       // 20MHz clock speed
    config.pixel_format = PIXFORMAT_JPEG; // JPEG format me photos lenge

    // Image quality set karte hain based on available memory
    if (psramFound())
    {
        config.frame_size = FRAMESIZE_UXGA; // UXGA resolution (1600x1200)
        config.jpeg_quality = 10;           // Best quality (0-63, where lower is better)
        config.fb_count = 2;                // 2 frame buffers
    }
    else
    {
        config.frame_size = FRAMESIZE_SVGA; // SVGA resolution (800x600)
        config.jpeg_quality = 12;           // Good quality
        config.fb_count = 1;                // 1 frame buffer
    }

    // Camera initialize karte hain
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera initialize nahi ho paya! Error: 0x%x\n", err);
        return false;
    }

    Serial.println("Camera successfully initialize ho gaya!");
    return true;
}

// Photo capture karne ka function
void capturePhoto()
{
    // Flash LED on karte hain
    digitalWrite(FLASH_LED, HIGH);
    delay(100); // Flash ke liye thoda wait karte hain

    // Photo capture karte hain
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("Photo capture nahi ho paya!");
        digitalWrite(FLASH_LED, LOW);
        return;
    }

    // Photo ki information print karte hain
    Serial.println("Photo capture ho gaya!");
    Serial.printf("Photo size: %d bytes\n", fb->len);
    Serial.printf("Resolution: %dx%d\n", fb->width, fb->height);

    // Memory free karte hain
    esp_camera_fb_return(fb);

    // Flash LED off karte hain
    digitalWrite(FLASH_LED, LOW);
}

// WiFi ke different status codes ko human readable format me convert karne ka function
void printWiFiStatus(wl_status_t status)
{
    // ... (same as Level 1) ...
}

// WiFi se connect karne ka function
bool tryConnectWiFi(const char *ssid, const char *password)
{
    // ... (same as Level 1) ...
}

void setup()
{
    // Brownout detector ko disable karte hain
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // LED pins ko output mode me set karte hain
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(FLASH_LED, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // LED off karte hain shuru me
    digitalWrite(FLASH_LED, LOW);    // Flash LED off karte hain

    // Serial communication start karte hain
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n\n=================================");
    Serial.println("ESP32 WiFi + Camera Test Level 2");
    Serial.println("=================================");

    // Camera initialize karte hain
    if (!initCamera())
    {
        Serial.println("Camera initialize nahi ho paya! Program rok rahe hain.");
        while (1)
        {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100); // Fast blinking to show error
        }
    }

    // WiFi connection ka code (same as Level 1)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(1000);

    bool connected = false;
    while (!connected)
    {
        // ... (same WiFi connection code as Level 1) ...
    }
}

void loop()
{
    static unsigned long lastCaptureTime = 0;     // Last photo capture ka time track karne ke liye
    const unsigned long CAPTURE_INTERVAL = 10000; // Har 10 seconds me photo lenge

    // Connected hai to photo capture karenge
    if (WiFi.status() == WL_CONNECTED)
    {
        digitalWrite(LED_BUILTIN, LOW); // LED on rakhenge jab connected ho

        // Check if it's time to capture a photo
        unsigned long currentTime = millis();
        if (currentTime - lastCaptureTime >= CAPTURE_INTERVAL)
        {
            Serial.println("\nPhoto capture kar rahe hain...");
            capturePhoto();
            lastCaptureTime = currentTime;
        }

        Serial.printf("Connected chal raha hai | Signal: %d dBm | IP: %s\n",
                      WiFi.RSSI(),
                      WiFi.localIP().toString().c_str());
    }
    else
    {
        digitalWrite(LED_BUILTIN, HIGH);
        Serial.println("Connection lost ho gaya! ESP32 ko restart kar rahe hain...");
        ESP.restart();
    }

    delay(1000); // 1 second wait
}