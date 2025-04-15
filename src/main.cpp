/*
ESP32-S3-DevKitc-1-N16R8 WebSocket Streaming with Microphone
===========================================================

Description:
-----------
1. WiFi:
   - Connection to specified WiFi network
   - Auto-reconnect on disconnection

2. Microphone Features:
   - Electrobot INMP441 I2S MEMS microphone integration
   - 44.1KHz sample rate, 16-bit samples
   - Audio level visualization with built-in LED
   - Audio streaming via WebSocket

3. WebSocket Features:
   - Live audio streaming to Node.js server
   - Microphone control through web interface
   - Bi-directional communication
   - Auto-reconnect on disconnection

Hardware Connections:
------------------
INMP441    ESP32-S3
--------   --------
VDD        3.3V
GND        GND
SCK        GPIO12
WS         GPIO11
SD         GPIO10
L/R        GND (for left channel)

Usage:
-----
1. Connect the microphone as shown above
2. Upload code to ESP32-S3-DevKitc-1
3. Start Node.js server: node server/server.js
4. Open web browser: http://localhost:3012
5. Enable microphone in the web interface
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include <algorithm>
#include <cmath>
#include <ArduinoJson.h>

// INMP441 Microphone pins - Using the pins specified by the user
#define I2S_SD 10
#define I2S_WS 11
#define I2S_SCK 12
#define I2S_PORT I2S_NUM_0

// Audio buffer settings
#define BUFFER_COUNT 10
#define BUFFER_LENGTH 1024
#define SAMPLE_RATE 16000
int16_t sBuffer[BUFFER_LENGTH];
int16_t testBuffer[BUFFER_LENGTH];

// WebSocket server details - Using SSL on port 443
const char *wsHost = "patr.ppcandles.in";
const int wsPort = 443;
const char *wsPath = "/";

// WiFi credentials
const char *ssid = "Tenda_1963D0";
const char *password = "ashish20032300";

// Global variables
WebSocketsClient webSocket;
bool isWebSocketConnected = false;
bool isMicrophoneEnabled = false;
bool isMicrophoneConnected = false;
const int LED_PIN = 2;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 5000;

// Standard audio packet format
#define PACKET_HEADER_MAGIC 0xA5 // Magic byte to identify our packets
#define PACKET_TYPE_AUDIO 0x01   // Audio packet type
#define PACKET_HEADER_SIZE 8     // 8-byte header for more robustness

// Function prototypes
void webSocketEvent(WStype_t type, uint8_t *payload, size_t length);
void setupMicrophone();
void microphoneTask(void *parameter);
bool checkMicrophoneConnection();

void setup()
{
    // Initialize serial communication
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\nESP32-S3 Audio WebSocket Client");
    Serial.println("--------------------------------");

    // Set LED pin mode
    pinMode(LED_PIN, OUTPUT);
    analogWrite(LED_PIN, 255); // Turn off LED initially

    // Connect to Wi-Fi
    Serial.printf("Connecting to WiFi network: %s\n", ssid);
    WiFi.begin(ssid, password);

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.print("WiFi connected, IP address: ");
    Serial.println(WiFi.localIP());

    // Set up WebSocket client
    Serial.printf("Connecting to WebSocket server: %s:%d%s\n", wsHost, wsPort, wsPath);
    webSocket.beginSSL(wsHost, wsPort, wsPath);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
    webSocket.enableHeartbeat(15000, 3000, 2);

    // Start microphone task on core 0
    xTaskCreatePinnedToCore(
        microphoneTask,   // Task function
        "MicrophoneTask", // Task name
        10000,            // Stack size (bytes)
        NULL,             // Task parameters
        1,                // Task priority
        NULL,             // Task handle
        0                 // Core ID (0)
    );

    Serial.println("Setup complete");
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi disconnected, reconnecting...");
        WiFi.reconnect();
        delay(1000);
        return;
    }

    webSocket.loop();

    static unsigned long lastStatusTime = 0;
    if (millis() - lastStatusTime > 10000)
    { // Reduced frequency of status messages
        lastStatusTime = millis();
        Serial.printf("WS:%s | Mic:%s | RSSI:%d\n",
                      isWebSocketConnected ? "ON" : "OFF",
                      isMicrophoneEnabled ? "ON" : "OFF",
                      WiFi.RSSI());

        if (!isWebSocketConnected && millis() - lastReconnectAttempt > RECONNECT_INTERVAL)
        {
            lastReconnectAttempt = millis();
            Serial.println("Reconnecting WebSocket...");
            webSocket.disconnect();
            delay(100);
            webSocket.beginSSL(wsHost, wsPort, wsPath);
        }
    }
}

// Function to check if microphone is properly connected and working
bool checkMicrophoneConnection()
{
    size_t bytesRead = 0;
    bool hasMic = false;
    int retries = 3;

    Serial.println("Checking microphone connection...");

    // Try multiple times to account for initialization
    for (int attempt = 0; attempt < retries; attempt++)
    {
        // Read some data from the microphone
        esp_err_t result = i2s_read(I2S_PORT, testBuffer, sizeof(testBuffer), &bytesRead, 100);

        if (result == ESP_OK && bytesRead > 0)
        {
            // Check if we have any non-zero values (indicating a working mic)
            int nonZeroCount = 0;
            int16_t maxValue = 0;

            for (int i = 0; i < bytesRead / 2; i++)
            {
                if (testBuffer[i] != 0)
                {
                    nonZeroCount++;
                    int16_t absVal = abs(testBuffer[i]);
                    if (absVal > maxValue)
                        maxValue = absVal;
                }
            }

            if (nonZeroCount > 10)
            { // Require at least 10 non-zero samples
                Serial.printf("Microphone OK: %d non-zero samples\n", nonZeroCount);
                hasMic = true;
                break;
            }
            else
            {
                Serial.printf("Attempt %d: Not enough samples (%d)\n", attempt + 1, nonZeroCount);
            }
        }
        else
        {
            Serial.printf("Attempt %d: Failed to read from mic\n", attempt + 1);
        }

        delay(100); // Wait a bit before retrying
    }

    return hasMic;
}

// Function to initialize I2S for INMP441 microphone
void setupMicrophone()
{
    Serial.println("Initializing INMP441 MEMS microphone...");

    // Configure I2S
    const i2s_config_t i2s_config = {
        .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = i2s_bits_per_sample_t(16),
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags = 0,
        .dma_buf_count = BUFFER_COUNT,
        .dma_buf_len = BUFFER_LENGTH,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0};

    // Install I2S driver
    esp_err_t result = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (result != ESP_OK)
    {
        Serial.printf("Failed to install I2S driver: %d\n", result);
        return;
    }

    // Configure I2S pins
    const i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = -1,
        .data_in_num = I2S_SD};

    // Set I2S pins
    result = i2s_set_pin(I2S_PORT, &pin_config);
    if (result != ESP_OK)
    {
        Serial.printf("Failed to set I2S pins: %d\n", result);
        return;
    }

    // Start I2S
    result = i2s_start(I2S_PORT);
    if (result != ESP_OK)
    {
        Serial.printf("Failed to start I2S: %d\n", result);
        return;
    }

    Serial.println("I2S microphone initialized successfully");
}

// Microphone task - runs on core 0
void microphoneTask(void *parameter)
{
    // Initialize microphone
    esp_err_t err;

    // Configure I2S for INMP441 microphone - fixed format
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000, // 16KHz fixed sample rate
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0};

    // I2S pin configuration - fixed pins
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD};

    // Install and start I2S driver
    err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK)
    {
        Serial.printf("Error installing I2S driver: %d\n", err);
        vTaskDelete(NULL);
        return;
    }

    // Set I2S pins
    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK)
    {
        Serial.printf("Error setting I2S pins: %d\n", err);
        vTaskDelete(NULL);
        return;
    }

    Serial.println("I2S microphone initialized");

    // Fixed buffer sizes for consistency
    const int bufferLen = 512;             // Smaller buffer for less latency
    const int bufferBytes = bufferLen * 4; // 4 bytes per sample for 32-bit
    const size_t bytesPerSample = 2;       // 2 bytes per sample for 16-bit output

    // Buffer for audio samples (32-bit I2S data)
    int32_t *audioBuffer32 = (int32_t *)malloc(bufferBytes);
    int16_t *audioBuffer16 = (int16_t *)malloc(bufferLen * bytesPerSample);

    // WebSocket buffer (header + audio data)
    // Header: [magic(1), type(1), seqNum(2), samples(2), checksum(2)]
    const size_t wsBufferSize = PACKET_HEADER_SIZE + (bufferLen * bytesPerSample);
    uint8_t *wsBuffer = (uint8_t *)malloc(wsBufferSize);

    if (!audioBuffer32 || !audioBuffer16 || !wsBuffer)
    {
        Serial.println("Failed to allocate memory for audio buffers");
        if (audioBuffer32)
            free(audioBuffer32);
        if (audioBuffer16)
            free(audioBuffer16);
        if (wsBuffer)
            free(wsBuffer);
        vTaskDelete(NULL);
        return;
    }

    Serial.println("Starting microphone task");
    int64_t lastStatusTime = 0;
    uint16_t packetSequence = 0; // Sequence number for packets

    // Main audio loop
    while (true)
    {
        // Check if WebSocket is connected and microphone is enabled
        if (isWebSocketConnected && isMicrophoneEnabled)
        {
            size_t bytesRead = 0;

            // Read audio data from I2S - fixed timeout
            esp_err_t result = i2s_read(I2S_PORT, audioBuffer32, bufferBytes, &bytesRead, 100);

            if (result == ESP_OK && bytesRead > 0)
            {
                // Calculate number of samples
                int samplesRead = bytesRead / 4; // 4 bytes per 32-bit sample

                // Process each sample with fixed gain to prevent glitches
                float gainFactor = 4.0; // Fixed gain to boost signal
                int16_t maxAbs = 0;
                float sumSquared = 0;

                // Convert 32-bit to 16-bit with consistent scaling
                for (int i = 0; i < samplesRead; i++)
                {
                    // Get the high 16 bits from the 32-bit sample (shift right by 16)
                    int32_t sample32 = audioBuffer32[i];

                    // Apply gain and convert to 16-bit
                    int32_t adjusted = (sample32 >> 16) * gainFactor;

                    // Clip to 16-bit range to prevent overflow
                    if (adjusted > 32767)
                        adjusted = 32767;
                    if (adjusted < -32768)
                        adjusted = -32768;

                    // Store as 16-bit
                    int16_t sample16 = (int16_t)adjusted;
                    audioBuffer16[i] = sample16;

                    // Calculate audio metrics
                    int16_t abs_sample = abs(sample16);
                    if (abs_sample > maxAbs)
                        maxAbs = abs_sample;
                    sumSquared += (float)sample16 * sample16;
                }

                // Calculate RMS - will be used for silence detection
                float rms = 0;
                if (samplesRead > 0)
                {
                    rms = sqrt(sumSquared / samplesRead);
                }

                // Only send if we have meaningful audio (not silence)
                if (maxAbs > 300 || rms > 100)
                {
                    // Create standardized audio packet with header
                    // Header: [magic(1), type(1), seqNum-high(1), seqNum-low(1),
                    //          samples-high(1), samples-low(1), checksum-high(1), checksum-low(1)]

                    // 1. Magic byte and packet type
                    wsBuffer[0] = PACKET_HEADER_MAGIC; // Fixed magic byte
                    wsBuffer[1] = PACKET_TYPE_AUDIO;   // Audio packet type

                    // 2. Sequence number (2 bytes, big-endian)
                    wsBuffer[2] = (packetSequence >> 8) & 0xFF; // High byte
                    wsBuffer[3] = packetSequence & 0xFF;        // Low byte

                    // 3. Sample count (2 bytes, big-endian)
                    wsBuffer[4] = (samplesRead >> 8) & 0xFF; // High byte
                    wsBuffer[5] = samplesRead & 0xFF;        // Low byte

                    // 4. Copy audio data after header
                    memcpy(wsBuffer + PACKET_HEADER_SIZE, audioBuffer16, samplesRead * bytesPerSample);

                    // 5. Calculate simple checksum (sum of all samples)
                    uint16_t checksum = 0;
                    for (int i = 0; i < samplesRead; i++)
                    {
                        checksum += abs(audioBuffer16[i]);
                    }

                    // 6. Store checksum in header (2 bytes, big-endian)
                    wsBuffer[6] = (checksum >> 8) & 0xFF; // High byte
                    wsBuffer[7] = checksum & 0xFF;        // Low byte

                    // Send the complete packet over WebSocket
                    size_t packetSize = PACKET_HEADER_SIZE + (samplesRead * bytesPerSample);
                    webSocket.sendBIN(wsBuffer, packetSize);

                    // Visual feedback - LED brightness shows audio level
                    int brightness = map(min((int)rms, 5000), 0, 5000, 0, 255);
                    analogWrite(LED_PIN, 255 - brightness);

                    // Increment sequence number for next packet
                    packetSequence++;

                    // Status logging (every 2 seconds)
                    int64_t now = esp_timer_get_time() / 1000;
                    if (now - lastStatusTime > 2000)
                    {
                        Serial.printf("Audio packet #%u: %d samples, Max: %d, RMS: %.1f\n",
                                      packetSequence, samplesRead, maxAbs, rms);
                        lastStatusTime = now;
                    }
                }
                else
                {
                    // No significant audio - ensure the LED shows this state
                    analogWrite(LED_PIN, 200); // Dim LED when silent
                }
            }
            else if (result != ESP_OK)
            {
                // Error reading from microphone
                Serial.printf("I2S read error: %d\n", result);
                delay(10);
            }
        }
        else
        {
            // Not connected or microphone disabled - update LED
            if (!isWebSocketConnected)
            {
                analogWrite(LED_PIN, 64); // Red - not connected
            }
            else if (!isMicrophoneEnabled)
            {
                analogWrite(LED_PIN, 0); // Blue - connected but mic disabled
            }

            delay(100);
        }

        // Small delay to prevent task watchdog timeouts
        delay(1);
    }

    // Cleanup (though this task should never end)
    free(audioBuffer32);
    free(audioBuffer16);
    free(wsBuffer);
    i2s_driver_uninstall(I2S_PORT);
    vTaskDelete(NULL);
}

// WebSocket event handler
void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_DISCONNECTED:
        Serial.println("WebSocket disconnected!");
        isWebSocketConnected = false;
        isMicrophoneEnabled = false; // Disable mic when disconnected
        break;

    case WStype_CONNECTED:
        Serial.println("WebSocket connected!");
        isWebSocketConnected = true;

        // Automatically enable microphone when connected
        Serial.println("Automatically enabling microphone");
        isMicrophoneEnabled = true;

        // Send identification message to server
        Serial.println("Sending device identification");
        webSocket.sendTXT("{\"type\":\"hello\",\"client\":\"esp32\",\"device\":\"ESP32-AUDIO\"}");
        break;

    case WStype_TEXT:
    {
        // We still handle commands in case the server sends them
        String message = String((char *)payload);
        Serial.printf("Received text message: %s\n", message.c_str());

        // Try to parse as JSON
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, message);
        if (!error)
        {
            // Handle commands if any
            if (doc.containsKey("command"))
            {
                String command = doc["command"].as<String>();

                if (command == "mic_on")
                {
                    Serial.println("Received mic_on command");
                    isMicrophoneEnabled = true;
                }
                else if (command == "mic_off")
                {
                    Serial.println("Received mic_off command");
                    isMicrophoneEnabled = false;
                }
            }
        }
    }
    break;

    case WStype_BIN:
        // We don't expect binary data from server
        Serial.printf("Received unexpected binary data: %u bytes\n", length);
        break;

    case WStype_ERROR:
        Serial.println("WebSocket error occurred!");
        break;

    default:
        break;
    }
}