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
#include <AudioTools.h>
#include <AudioI2S.h>

// WebSocket server details
const char *wsHost = "patr.ppcandles.in";
const int wsPort = 443;
const char *wsPath = "/";

// WiFi credentials
const char *ssid = "Tenda_1963D0";
const char *password = "ashish20032300";

// Audio Configuration
#define SAMPLE_RATE 16000
#define CHANNELS 1
#define BITS_PER_SAMPLE 16
#define I2S_PORT I2S_NUM_0

// I2S Pins
const int I2S_WS_PIN = 42;  // Word Select
const int I2S_SCK_PIN = 40; // Serial Clock
const int I2S_SD_PIN = 41;  // Serial Data

// Audio buffers
const int BUFFER_SIZE = 256;
uint8_t audioBuffer[BUFFER_SIZE];
int16_t samplesBuffer[BUFFER_SIZE / 2];

// Audio handling objects
I2SStream i2sStream;
StreamCopy copier;
AudioInfo audioInfo;

// Global variables
WebSocketsClient webSocket;
bool isWebSocketConnected = false;
bool isMicrophoneEnabled = false; // Start with mic disabled
const int LED_PIN = 2;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 5000;

// Function prototypes
bool connectWiFi();
void webSocketEvent(WStype_t type, uint8_t *payload, size_t length);
void microphoneTask(void *parameter);
void setupMicrophone();
void convert32to16(int32_t *samples32, int16_t *samples16, size_t count);
void IRAM_ATTR onAudioTimer();
void setupAudioTimer();
size_t compressAudioData(int16_t *input, size_t inputSize, uint8_t *output, size_t maxOutputSize);
void setupAudio();
bool readAudioData();

void setup()
{
    // Initialize serial
    Serial.begin(115200);
    delay(1000);

    Serial.println("\nESP32-S3-DevKitc-1-N16R8 WebSocket Streaming");
    Serial.println("==========================================");

    // Initialize built-in LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    Serial.printf("Built-in LED pin set to GPIO%d, initial state: ON\n", LED_PIN);

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

    // Setup WebSocket
    webSocket.beginSSL(wsHost, wsPort, wsPath);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
    webSocket.enableHeartbeat(15000, 3000, 2);

    // Setup Audio
    setupAudio();

    // Start microphone task on Core 1
    xTaskCreatePinnedToCore(
        microphoneTask,
        "microphoneTask",
        10000,
        NULL,
        1,
        NULL,
        1);

    // Setup audio timer
    setupAudioTimer();

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
    if (millis() - lastStatusTime > 5000)
    {
        lastStatusTime = millis();
        Serial.printf("Status: WebSocket connected=%s, Microphone enabled=%s, WiFi RSSI=%d\n",
                      isWebSocketConnected ? "YES" : "NO",
                      isMicrophoneEnabled ? "YES" : "NO",
                      WiFi.RSSI());

        if (!isWebSocketConnected && millis() - lastReconnectAttempt > RECONNECT_INTERVAL)
        {
            lastReconnectAttempt = millis();
            Serial.println("Attempting WebSocket reconnection...");
            webSocket.disconnect();
            delay(100);
            webSocket.beginSSL(wsHost, wsPort, wsPath);
        }
    }

    if (isWebSocketConnected && isMicrophoneEnabled)
    {
        readAudioData();
    }

    delay(1);
}

// Connect to WiFi network
bool connectWiFi()
{
    Serial.printf("Connecting to WiFi network: %s\n", ssid);

    WiFi.begin(ssid, password);

    // Wait for connection with timeout
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nWiFi connected successfully!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        return true;
    }
    else
    {
        Serial.println("\nFailed to connect to WiFi!");
        return false;
    }
}

// Function to initialize I2S for INMP441 microphone
void setupMicrophone()
{
    Serial.println("Initializing Electrobot INMP441 MEMS microphone...");
    Serial.printf("Using pins: SCK=%d, WS=%d, SD=%d\n", I2S_SCK_PIN, I2S_WS_PIN, I2S_SD_PIN);

    // Configure I2S
    const i2s_config_t i2s_config = {
        .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = i2s_bits_per_sample_t(32),
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = BUFFER_SIZE,
        .use_apll = true,
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
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = -1,
        .data_in_num = I2S_SD_PIN};

    // Set I2S pins
    result = i2s_set_pin(I2S_PORT, &pin_config);
    if (result != ESP_OK)
    {
        Serial.printf("Failed to set I2S pins: %d\n", result);
        return;
    }

    // Start I2S
    i2s_start(I2S_PORT);
    Serial.println("I2S microphone initialized successfully");
}

// Function to convert 32-bit I2S data to 16-bit PCM
void convert32to16(int32_t *samples32, int16_t *samples16, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        // Shift 32-bit integer to 16-bit
        samples16[i] = samples32[i] >> 16;
    }
}

// Function to compress audio data
size_t compressAudioData(int16_t *input, size_t inputSize, uint8_t *output, size_t maxOutputSize)
{
    if (!aac)
    {
        aac = new AudioGeneratorAAC();
        out = new AudioOutputI2S();
        out->SetGain(0.5);
        out->SetRate(44100);
        out->SetBitsPerSample(16);
        out->SetChannels(1);
    }

    size_t compressedSize = 0;
    if (aac->begin(input, inputSize, out))
    {
        // Copy compressed data to output buffer
        memcpy(output, aac->getBuffer(), aac->getBufferSize());
        compressedSize = aac->getBufferSize();
    }

    return compressedSize;
}

// Microphone task runs on separate core
void microphoneTask(void *parameter)
{
    // Initialize microphone hardware
    setupMicrophone();

    size_t bytesRead = 0;
    uint32_t lastPrintTime = 0;
    int32_t samples32[BUFFER_SIZE];        // Buffer for 32-bit samples
    int16_t samples16[BUFFER_SIZE];        // Buffer for converted 16-bit samples
    uint8_t compressedBuffer[BUFFER_SIZE]; // Buffer for compressed audio

    // Create buffer for audio header (6 bytes)
    uint8_t header[6];
    header[0] = 1; // Data type: 1 = audio
    header[5] = 0; // Reserved

    // Main microphone loop
    while (true)
    {
        if (isWebSocketConnected && isMicrophoneEnabled)
        {
            // Read 32-bit audio data from I2S
            esp_err_t result = i2s_read(I2S_PORT, samples32, sizeof(samples32), &bytesRead, portMAX_DELAY);

            if (result == ESP_OK && bytesRead > 0)
            {
                size_t samplesRead = bytesRead / 4; // 32-bit = 4 bytes per sample

                // Convert 32-bit samples to 16-bit
                for (size_t i = 0; i < samplesRead; i++)
                {
                    // Convert 32-bit to 16-bit with proper scaling
                    samples16[i] = samples32[i] >> 16;
                }

                // Compress audio data
                size_t compressedSize = compressAudioData(samples16, samplesRead * 2, compressedBuffer, sizeof(compressedBuffer));

                if (compressedSize > 0)
                {
                    // Send compressed audio data
                    header[1] = (compressedSize >> 0) & 0xFF;
                    header[2] = (compressedSize >> 8) & 0xFF;
                    header[3] = (compressedSize >> 16) & 0xFF;
                    header[4] = (compressedSize >> 24) & 0xFF;

                    if (webSocket.sendBIN(header, 6))
                    {
                        delay(1);
                        if (!webSocket.sendBIN(compressedBuffer, compressedSize))
                        {
                            Serial.println("Failed to send compressed audio data");
                        }
                    }
                }

                // Calculate audio metrics
                int32_t maxAmp = 0;
                int64_t sumSquares = 0; // Use 64-bit to prevent overflow

                for (size_t i = 0; i < samplesRead; i++)
                {
                    int16_t sample = samples16[i];
                    int32_t amplitude = abs(sample);
                    maxAmp = max(maxAmp, amplitude);
                    sumSquares += (int64_t)sample * sample;
                }

                // Calculate RMS with proper scaling
                int32_t rms = sqrt(sumSquares / samplesRead);

                // Map RMS to LED brightness (non-linear)
                int brightness = map(rms, 0, 32767 / 4, 0, 255);
                brightness = pow(brightness / 255.0, 1.5) * 255;
                analogWrite(LED_PIN, 255 - brightness);

                // Debug info approximately once per second
                if (millis() - lastPrintTime > 1000)
                {
                    lastPrintTime = millis();
                    Serial.printf("Audio sent: %d bytes, max amplitude: %d, RMS: %d, LED: %d\n",
                                  compressedSize, maxAmp, rms, 255 - brightness);
                }
            }
            else if (result != ESP_OK)
            {
                Serial.printf("Error reading I2S data: %d\n", result);
                delay(100);
            }
        }
        else
        {
            digitalWrite(LED_PIN, HIGH);
            delay(100);
        }

        delay(1);
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
        break;

    case WStype_TEXT:
    {
        String command = String((char *)payload);
        command.trim();
        Serial.printf("Received command: '%s'\n", command.c_str());

        // Handle microphone commands
        if (command == "mic_on")
        {
            Serial.println("Microphone ON command received");
            isMicrophoneEnabled = true;
        }
        else if (command == "mic_off")
        {
            Serial.println("Microphone OFF command received");
            isMicrophoneEnabled = false;
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

void IRAM_ATTR onAudioTimer()
{
    portENTER_CRITICAL_ISR(&timerMux);
    // Set flag to indicate time to send audio
    shouldSendAudio = true;
    portEXIT_CRITICAL_ISR(&timerMux);
}

void setupAudioTimer()
{
    audioTimer = timerBegin(0, 80, true); // 80MHz / 80 = 1MHz
    timerAttachInterrupt(audioTimer, &onAudioTimer, true);
    timerAlarmWrite(audioTimer, AUDIO_TIMER_MS * 1000, true);
    timerAlarmEnable(audioTimer);
}

// Function to setup I2S audio
void setupAudio()
{
    Serial.println("Setting up I2S Audio...");

    auto config = i2sStream.defaultConfig();
    config.sample_rate = SAMPLE_RATE;
    config.bits_per_sample = BITS_PER_SAMPLE;
    config.channels = CHANNELS;
    config.i2s_format = I2S_STD_FORMAT;
    config.pin_ws = I2S_WS_PIN;
    config.pin_bck = I2S_SCK_PIN;
    config.pin_data = I2S_SD_PIN;
    config.buffer_size = BUFFER_SIZE;
    config.buffer_count = 8;

    // Open I2S input
    i2sStream.begin(config);

    audioInfo.sample_rate = SAMPLE_RATE;
    audioInfo.channels = CHANNELS;
    audioInfo.bits_per_sample = BITS_PER_SAMPLE;

    Serial.println("I2S Audio setup complete");
}

// Function to read audio data
bool readAudioData()
{
    size_t bytesRead = 0;
    if (i2sStream.available())
    {
        bytesRead = i2sStream.readBytes(audioBuffer, BUFFER_SIZE);

        if (bytesRead > 0)
        {
            // Convert bytes to int16_t samples
            for (int i = 0; i < bytesRead / 2; i++)
            {
                samplesBuffer[i] = (audioBuffer[i * 2 + 1] << 8) | audioBuffer[i * 2];
            }

            // Calculate audio metrics
            int32_t maxAmp = 0;
            float rms = 0;

            for (int i = 0; i < bytesRead / 2; i++)
            {
                int16_t sample = samplesBuffer[i];
                int32_t amplitude = abs(sample);
                maxAmp = max(maxAmp, amplitude);
                rms += sample * sample;
            }

            rms = sqrt(rms / (bytesRead / 2));

            // Send audio data via WebSocket
            if (isWebSocketConnected && isMicrophoneEnabled)
            {
                // Send header
                uint8_t header[6];
                header[0] = 1; // Audio data type
                uint32_t dataSize = bytesRead;
                header[1] = dataSize & 0xFF;
                header[2] = (dataSize >> 8) & 0xFF;
                header[3] = (dataSize >> 16) & 0xFF;
                header[4] = (dataSize >> 24) & 0xFF;
                header[5] = 0; // Reserved

                webSocket.sendBIN(header, 6);
                delay(1);
                webSocket.sendBIN(audioBuffer, bytesRead);

                // Debug output
                static uint32_t lastDebugTime = 0;
                if (millis() - lastDebugTime > 1000)
                {
                    lastDebugTime = millis();
                    Serial.printf("Audio: %d bytes, Max Amp: %d, RMS: %.2f\n",
                                  bytesRead, maxAmp, rms);
                }
            }
            return true;
        }
    }
    return false;
}