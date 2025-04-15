// ESP32-S3 WebSocket Audio Streaming
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>

// WiFi credentials
const char *ssid = "Tenda_1963D0";
const char *password = "ashish20032300";

// WebSocket server details
const char *wsHost = "patr.ppcandles.in";
const int wsPort = 443;
const char *wsPath = "/";

// Audio configuration
#define I2S_WS_PIN 42  // Word Select - GPIO42
#define I2S_SCK_PIN 40 // Serial Clock - GPIO40
#define I2S_SD_PIN 41  // Serial Data - GPIO41
#define I2S_PORT I2S_NUM_0

#define SAMPLE_RATE 44100
#define BITS_PER_SAMPLE 16
#define BUFFER_SIZE 1024
#define BUFFER_COUNT 8

// LED control
#define LED_PIN 35
#define LED_CHANNEL 0
#define LED_RESOLUTION 8
#define LED_FREQ 5000

// Global variables
WebSocketsClient webSocket;
TaskHandle_t microphoneTaskHandle = NULL;
bool microphoneEnabled = false;
bool wsConnected = false;
int reconnectCount = 0;

// Function prototypes
void microphoneTask(void *parameter);
void updateLED(uint32_t value);
int16_t convert32to16(int32_t sample);
float calculateRMS(int16_t *samples, size_t count);

// WiFi event handler
void WiFiEvent(WiFiEvent_t event)
{
    switch (event)
    {
    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.println("WiFi disconnected. Reconnecting...");
        WiFi.begin(ssid, password);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.print("WiFi connected. IP: ");
        Serial.println(WiFi.localIP());
        break;
    default:
        break;
    }
}

// WebSocket event handler
void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_DISCONNECTED:
        wsConnected = false;
        Serial.println("WebSocket disconnected!");
        break;

    case WStype_CONNECTED:
        wsConnected = true;
        reconnectCount = 0;
        Serial.println("WebSocket connected!");

        // Send device information
        DynamicJsonDocument doc(512);
        doc["type"] = "device_info";
        doc["device"] = "ESP32-S3";
        doc["mac"] = WiFi.macAddress();
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
        doc["version"] = "1.0.0";
        doc["audioEnabled"] = microphoneEnabled;

        String deviceInfo;
        serializeJson(doc, deviceInfo);
        webSocket.sendTXT(deviceInfo);
        break;

    case WStype_TEXT:
        // Handle incoming commands
        if (length > 0)
        {
            String command = String((char *)payload);
            Serial.print("Received command: ");
            Serial.println(command);

            // Parse as JSON
            DynamicJsonDocument doc(512);
            DeserializationError error = deserializeJson(doc, command);

            if (!error)
            {
                // Handle microphone enable/disable command
                if (doc["type"] == "command" && doc["action"] == "enableMic")
                {
                    bool enable = doc["value"].as<bool>();

                    if (enable && !microphoneEnabled)
                    {
                        // Start microphone
                        microphoneEnabled = true;
                        if (microphoneTaskHandle == NULL)
                        {
                            xTaskCreatePinnedToCore(
                                microphoneTask,
                                "MicrophoneTask",
                                4096,
                                NULL,
                                1,
                                &microphoneTaskHandle,
                                0);
                            Serial.println("Microphone task started");
                        }
                    }
                    else if (!enable && microphoneEnabled)
                    {
                        // Stop microphone
                        microphoneEnabled = false;
                        if (microphoneTaskHandle != NULL)
                        {
                            vTaskDelete(microphoneTaskHandle);
                            microphoneTaskHandle = NULL;
                            Serial.println("Microphone task stopped");
                        }
                    }

                    // Send status update
                    DynamicJsonDocument statusDoc(256);
                    statusDoc["type"] = "status";
                    statusDoc["microphoneEnabled"] = microphoneEnabled;
                    statusDoc["rssi"] = WiFi.RSSI();

                    String statusUpdate;
                    serializeJson(statusDoc, statusUpdate);
                    webSocket.sendTXT(statusUpdate);
                }
            }
        }
        break;

    case WStype_ERROR:
        Serial.println("WebSocket error!");
        break;

    default:
        break;
    }
}

void setup()
{
    // Initialize serial
    Serial.begin(115200);
    Serial.println("\nESP32-S3 Audio WebSocket Client");

    // Initialize LED
    ledcSetup(LED_CHANNEL, LED_FREQ, LED_RESOLUTION);
    ledcAttachPin(LED_PIN, LED_CHANNEL);
    updateLED(0);

    // Connect to WiFi
    WiFi.onEvent(WiFiEvent);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.print("Connected to WiFi. IP: ");
    Serial.println(WiFi.localIP());

    // Configure WebSocket client
    webSocket.beginSSL(wsHost, wsPort, wsPath);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
    webSocket.enableHeartbeat(15000, 3000, 2);

    // Configure I2S
    esp_err_t err;

    // Setup I2S config for reading from microphone
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = BUFFER_COUNT,
        .dma_buf_len = BUFFER_SIZE,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0};

    // Setup I2S pins
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD_PIN};

    // Install and start I2S driver
    err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK)
    {
        Serial.printf("Failed to install I2S driver: %d\n", err);
        return;
    }

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK)
    {
        Serial.printf("Failed to set I2S pins: %d\n", err);
        return;
    }

    // Set I2S clock
    err = i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
    if (err != ESP_OK)
    {
        Serial.printf("Failed to set I2S clock: %d\n", err);
        return;
    }

    Serial.println("I2S initialized successfully");
}

void loop()
{
    webSocket.loop();

    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi disconnected. Reconnecting...");
        WiFi.reconnect();
        delay(1000);
    }

    // Check WebSocket connection and send status updates
    static unsigned long lastStatusTime = 0;
    if (millis() - lastStatusTime > 10000)
    {
        lastStatusTime = millis();

        if (wsConnected)
        {
            // Send periodic status update
            DynamicJsonDocument statusDoc(256);
            statusDoc["type"] = "status";
            statusDoc["microphoneEnabled"] = microphoneEnabled;
            statusDoc["rssi"] = WiFi.RSSI();
            statusDoc["uptime"] = millis() / 1000;

            String statusUpdate;
            serializeJson(statusDoc, statusUpdate);
            webSocket.sendTXT(statusUpdate);
        }
        else
        {
            reconnectCount++;
            if (reconnectCount > 12)
            { // After ~2 minutes of failed reconnects
                Serial.println("WebSocket reconnection failed, restarting...");
                ESP.restart();
            }
        }
    }

    delay(10); // Small delay to avoid WDT resets
}

// Microphone task - runs on core 0
void microphoneTask(void *parameter)
{
    int32_t buffer32[BUFFER_SIZE];
    int16_t buffer16[BUFFER_SIZE];
    size_t bytesRead = 0;

    while (microphoneEnabled)
    {
        // Read audio data from I2S
        esp_err_t result = i2s_read(I2S_PORT, buffer32, sizeof(buffer32), &bytesRead, 100);

        if (result == ESP_OK && bytesRead > 0 && wsConnected)
        {
            // Convert 32-bit samples to 16-bit
            size_t samples = bytesRead / sizeof(int32_t);

            for (int i = 0; i < samples; i++)
            {
                buffer16[i] = convert32to16(buffer32[i]);
            }

            // Calculate audio level for LED
            float rmsLevel = calculateRMS(buffer16, samples);
            uint32_t ledValue = map(rmsLevel, 0, 10000, 0, 255);
            updateLED(ledValue);

            // Log stats occasionally
            static int packetCount = 0;
            packetCount++;

            if (packetCount % 100 == 0)
            {
                int16_t maxAmp = 0;
                for (int i = 0; i < samples; i++)
                {
                    if (abs(buffer16[i]) > maxAmp)
                    {
                        maxAmp = abs(buffer16[i]);
                    }
                }

                Serial.printf("Audio packet #%d: %d samples, Max: %d, RMS: %.2f\n",
                              packetCount, samples, maxAmp, rmsLevel);
            }

            // More efficient JSON serialization for audio data
            String jsonStart = "{\"type\":\"audio\",\"format\":\"pcm\",\"sampleRate\":";
            jsonStart += SAMPLE_RATE;
            jsonStart += ",\"data\":[";

            String jsonEnd = "]}";
            String samples_str = "";

            // Build the data array with a more efficient approach
            for (int i = 0; i < samples; i++)
            {
                if (i > 0)
                {
                    samples_str += ",";
                }
                samples_str += String(buffer16[i]);

                // Send in chunks if the string gets too long
                if (samples_str.length() > 1024)
                {
                    if (i < samples - 1)
                    {
                        String packet = jsonStart + samples_str + jsonEnd;
                        webSocket.sendTXT(packet);
                        samples_str = "";
                        jsonStart = "{\"type\":\"audio\",\"format\":\"pcm\",\"sampleRate\":";
                        jsonStart += SAMPLE_RATE;
                        jsonStart += ",\"data\":[";
                    }
                }
            }

            // Send the final packet
            if (samples_str.length() > 0)
            {
                String packet = jsonStart + samples_str + jsonEnd;
                webSocket.sendTXT(packet);
            }
        }
        else if (result != ESP_OK)
        {
            Serial.printf("I2S read error: %d\n", result);
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        // Small delay to allow other tasks to run
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }

    // Task is being deleted
    microphoneTaskHandle = NULL;
    vTaskDelete(NULL);
}

// Update LED based on audio level
void updateLED(uint32_t value)
{
    // Non-linear scaling for better visual response
    uint32_t scaledValue = value * value / 255;
    if (scaledValue > 255)
        scaledValue = 255;

    ledcWrite(LED_CHANNEL, scaledValue);
}

// Convert 32-bit I2S sample to 16-bit PCM
int16_t convert32to16(int32_t sample)
{
    // The INMP441 sends audio data in high 24 bits of the 32-bit word
    // Shift right by 16 to get the most significant 16 bits
    return sample >> 16;
}

// Calculate RMS of audio samples
float calculateRMS(int16_t *samples, size_t count)
{
    float sum = 0;

    for (size_t i = 0; i < count; i++)
    {
        sum += samples[i] * samples[i];
    }

    return sqrt(sum / count);
}