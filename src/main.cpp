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

// INMP441 Microphone pins - Using the pins specified by the user
#define I2S_SD 10
#define I2S_WS 11
#define I2S_SCK 12
#define I2S_PORT I2S_NUM_0

// Audio buffer settings
#define BUFFER_COUNT 10
#define BUFFER_LENGTH 1024
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

// Function prototypes
void webSocketEvent(WStype_t type, uint8_t *payload, size_t length);
void setupMicrophone();
void microphoneTask(void *parameter);
bool checkMicrophoneConnection();

void setup()
{
    // Initialize serial
    Serial.begin(115200);
    delay(1000);

    Serial.println("\nESP32-S3 WebSocket Audio Streaming with INMP441");
    Serial.println("================================================");

    // Initialize built-in LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    Serial.printf("Built-in LED pin set to GPIO%d\n", LED_PIN);

    // Setup microphone first to check connection
    setupMicrophone();

    // Check if microphone is connected and auto-enable if it is
    isMicrophoneConnected = checkMicrophoneConnection();
    if (isMicrophoneConnected)
    {
        Serial.println("Microphone detected! Auto-enabling the microphone.");
        isMicrophoneEnabled = true;
        digitalWrite(LED_PIN, LOW); // Turn on LED to indicate mic is ready
    }
    else
    {
        Serial.println("Microphone not detected or not functioning properly.");
        Serial.println("Please check connections and restart, or wait for manual activation.");
        digitalWrite(LED_PIN, HIGH); // LED off
    }

    // Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.printf("Connecting to WiFi network: %s\n", ssid);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Setup WebSocket with SSL
    webSocket.beginSSL(wsHost, wsPort, wsPath);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);

    // Start microphone task on Core 1
    xTaskCreatePinnedToCore(
        microphoneTask,
        "microphoneTask",
        10000,
        NULL,
        1,
        NULL,
        1);

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
        .sample_rate = 44100,
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

// Microphone task runs on separate core
void microphoneTask(void *parameter)
{
    size_t bytesRead = 0;
    uint32_t lastPrintTime = 0;
    static int packetCounter = 0;

    // Main microphone loop
    while (true)
    {
        if (isWebSocketConnected && isMicrophoneEnabled)
        {
            // Read audio data from I2S
            esp_err_t result = i2s_read(I2S_PORT, sBuffer, sizeof(sBuffer), &bytesRead, portMAX_DELAY);

            if (result == ESP_OK && bytesRead > 0)
            {
                // Check if we have any non-zero samples
                bool hasNonZeroSamples = false;
                int16_t maxSample = 0;
                int16_t rms = 0;

                for (size_t i = 0; i < bytesRead / 2; i++)
                {
                    if (sBuffer[i] != 0)
                    {
                        hasNonZeroSamples = true;
                        int16_t absValue = abs(sBuffer[i]);
                        if (absValue > maxSample)
                        {
                            maxSample = absValue;
                        }
                        rms += (int32_t)sBuffer[i] * sBuffer[i];
                    }
                }

                if (hasNonZeroSamples)
                {
                    // Calculate RMS
                    rms = sqrt(rms / (bytesRead / 2));

                    // Send audio data - format to match server expectations
                    // Prepare a JSON packet with PCM data
                    String jsonStart = "{\"type\":\"audio\",\"format\":\"pcm\",\"sampleRate\":44100,\"data\":[";
                    String jsonEnd = "]}";

                    // Send some samples (not all, to avoid large packets)
                    const int maxSamplesToSend = 512; // Limit packet size
                    String samples = "";

                    for (int i = 0; i < min((int)(bytesRead / 2), maxSamplesToSend); i += 4)
                    {
                        // Only send every 4th sample to reduce data volume
                        if (samples.length() > 0)
                            samples += ",";
                        samples += String(sBuffer[i]);
                    }

                    // Send the complete JSON packet
                    String packet = jsonStart + samples + jsonEnd;
                    webSocket.sendTXT(packet);

                    // Light up LED based on audio level
                    int brightness = map(maxSample, 0, 32767 / 4, 0, 255);
                    analogWrite(LED_PIN, 255 - brightness);

                    // Debug output occasionally
                    if (millis() - lastPrintTime > 5000)
                    {
                        lastPrintTime = millis();
                        packetCounter++;
                        Serial.printf("Audio: %d bytes, Max: %d\n", bytesRead, maxSample);
                    }
                }
                else
                {
                    digitalWrite(LED_PIN, HIGH); // Turn off LED if no audio
                    if (millis() - lastPrintTime > 10000)
                    {
                        lastPrintTime = millis();
                        Serial.println("No audio detected - check mic");

                        // Re-check microphone if no audio for a while
                        isMicrophoneConnected = checkMicrophoneConnection();
                    }
                }
            }
            else if (result != ESP_OK)
            {
                if (millis() - lastPrintTime > 5000)
                {
                    lastPrintTime = millis();
                    Serial.printf("I2S read error: %d\n", result);
                }
                delay(100);
            }
        }
        else
        {
            digitalWrite(LED_PIN, HIGH); // LED off when mic is disabled
            delay(100);
        }

        // Small delay to prevent watchdog timeouts
        delay(10);
    }
}

// WebSocket event handler
void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_DISCONNECTED:
        Serial.println("WebSocket disconnected!");
        isWebSocketConnected = false;
        break;

    case WStype_CONNECTED:
        Serial.println("WebSocket connected!");
        isWebSocketConnected = true;

        // Send microphone status message
        webSocket.sendTXT("{\"type\":\"info\",\"message\":\"ESP32 microphone connected\"}");
        break;

    case WStype_TEXT:
    {
        String command = String((char *)payload);
        command.trim();

        // Handle commands without verbose logging
        if (command == "mic_on")
        {
            isMicrophoneEnabled = true;
        }
        else if (command == "mic_off")
        {
            isMicrophoneEnabled = false;
            digitalWrite(LED_PIN, HIGH); // Turn off LED when mic is disabled
        }
        else if (command == "mic_check")
        {
            // Re-check microphone
            isMicrophoneConnected = checkMicrophoneConnection();
            String status = "{\"type\":\"mic_status\",\"connected\":" +
                            String(isMicrophoneConnected ? "true" : "false") +
                            ",\"enabled\":" +
                            String(isMicrophoneEnabled ? "true" : "false") + "}";
            webSocket.sendTXT(status);
        }
    }
    break;

    case WStype_BIN:
    case WStype_ERROR:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    default:
        break;
    }
}