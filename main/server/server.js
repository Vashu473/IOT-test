const express = require("express");
const http = require("http");
const WebSocket = require("ws");
const path = require("path");
const fs = require("fs");
const { createCanvas } = require("canvas");

const app = express();
const server = http.createServer(app);
const wss = new WebSocket.Server({
  server,
  perMessageDeflate: false,
  clientTracking: true,
  maxPayload: 50 * 1024 * 1024, // 50MB max payload
});

// Serve static files from public directory
app.use(express.static(path.join(__dirname, "public")));

// Store connected clients
const clients = new Set();

// Audio processing settings
const AUDIO_SAMPLE_RATE = 16000; // Match with ESP32
const AUDIO_CHANNELS = 1;
const AUDIO_BIT_DEPTH = 16;

function convertRGB565ToJPEG(buffer, width, height) {
  const canvas = createCanvas(width, height);
  const ctx = canvas.getContext("2d");
  const imageData = ctx.createImageData(width, height);

  // Convert RGB565 to RGBA
  for (let i = 0; i < buffer.length; i += 2) {
    const high = buffer[i + 1];
    const low = buffer[i];
    const pixel = (high << 8) | low;

    const r = ((pixel >> 11) & 0x1f) << 3; // 5 bits red
    const g = ((pixel >> 5) & 0x3f) << 2; // 6 bits green
    const b = (pixel & 0x1f) << 3; // 5 bits blue

    const j = (i / 2) * 4;
    imageData.data[j] = r;
    imageData.data[j + 1] = g;
    imageData.data[j + 2] = b;
    imageData.data[j + 3] = 255; // Alpha channel
  }

  ctx.putImageData(imageData, 0, 0);
  return canvas.toDataURL("image/jpeg", 0.8); // 80% JPEG quality
}

// Function to process raw PCM audio data
function processAudioData(buffer) {
  console.log(`Processing audio data: ${buffer.length} bytes`);

  try {
    const processedBuffer = Buffer.from(buffer);
    let maxVal = 0;
    let rms = 0;

    // Process 16-bit PCM data
    const int16View = new Int16Array(
      processedBuffer.buffer,
      processedBuffer.byteOffset,
      processedBuffer.byteLength / 2
    );

    // Calculate audio metrics
    for (let i = 0; i < int16View.length; i++) {
      const val = int16View[i];
      const amplitude = Math.abs(val);
      maxVal = Math.max(maxVal, amplitude);
      rms += val * val;
    }
    rms = Math.sqrt(rms / int16View.length);

    // Simple noise gate
    const noiseThreshold = Math.max(200, rms * 0.1);
    for (let i = 0; i < int16View.length; i++) {
      if (Math.abs(int16View[i]) < noiseThreshold) {
        int16View[i] = 0;
      }
    }

    console.log(
      `Audio processed. Max amplitude: ${maxVal}, RMS: ${rms.toFixed(2)}`
    );

    return {
      processedBuffer,
      maxAmplitude: maxVal,
      rms: rms,
    };
  } catch (error) {
    console.error(`Error processing audio data: ${error.message}`);
    return null;
  }
}

// WebSocket connection handling with improved error handling and performance
wss.on("connection", (ws, req) => {
  const clientId = Math.random().toString(36).substring(7);
  const isESP32 =
    req.headers["user-agent"] && req.headers["user-agent"].includes("arduino");

  ws.isAlive = true;
  ws.clientId = clientId;
  ws.isESP32 = isESP32;
  ws.lastPing = Date.now();
  clients.add(ws);

  // Variables for handling image data with improved memory management
  let expectedSize = 0;
  let imageBuffer = null;
  let receivedSize = 0;
  let lastFrameTime = Date.now();
  let frameCount = 0;
  let fps = 0;

  // Variables for audio handling
  let audioBuffer = null;
  let audioExpectedSize = 0;
  let audioReceivedSize = 0;
  let audioSequence = 0;

  // Heartbeat check
  const heartbeat = setInterval(() => {
    if (ws.isAlive === false) {
      return ws.terminate();
    }
    ws.isAlive = false;
    ws.ping();
  }, 30000);

  ws.on("pong", () => {
    ws.isAlive = true;
    ws.lastPing = Date.now();
  });

  ws.on("message", (data) => {
    try {
      // Handle flash commands
      if (data === "flash_on" || data === "flash_off") {
        // Broadcast flash command to ESP32 with error handling
        let espClientFound = false;
        wss.clients.forEach((client) => {
          if (client.isESP32 && client.readyState === WebSocket.OPEN) {
            try {
              client.send(data);
              espClientFound = true;
            } catch (e) {
              // Error handling
            }
          }
        });

        if (!espClientFound) {
          ws.send(
            JSON.stringify({
              type: "error",
              message: "No camera connected to receive flash command",
            })
          );
        } else {
          ws.send(
            JSON.stringify({
              type: "info",
              message: `${data} command sent to camera`,
            })
          );
        }
        return;
      }

      // Handle legacy flash toggle command
      if (data === "flash") {
        // Forward as flash_on for compatibility
        wss.clients.forEach((client) => {
          if (client.isESP32 && client.readyState === WebSocket.OPEN) {
            try {
              client.send("flash_on");
            } catch (e) {
              // Error handling
            }
          }
        });
        return;
      }

      // Handle microphone commands
      if (data === "mic_on" || data === "mic_off") {
        console.log(`MIC COMMAND received from browser: ${data}`); // Keep mic logs

        // Broadcast mic command to ESP32
        let espClientFound = false;
        wss.clients.forEach((client) => {
          if (client.isESP32 && client.readyState === WebSocket.OPEN) {
            try {
              console.log(
                `Sending ${data} command to ESP32 client ${client.clientId}`
              ); // Keep mic logs
              client.send(data);
              espClientFound = true;
            } catch (e) {
              console.error(
                `Error sending ${data} command to ESP32: ${e.message}`
              ); // Keep mic error logs
            }
          }
        });

        if (!espClientFound) {
          console.warn("No ESP32 clients connected to receive mic command"); // Keep mic logs
          ws.send(
            JSON.stringify({
              type: "error",
              message: "No device connected to receive mic command",
            })
          );
        } else {
          ws.send(
            JSON.stringify({
              type: "info",
              message: `${data} command sent to device`,
            })
          );
        }
        return;
      }

      if (!Buffer.isBuffer(data)) {
        return;
      }

      // Check for header pattern to identify data type
      if (data.length === 6) {
        // Format: [dataType (1 byte)][size (4 bytes)][reserved (1 byte)]
        const dataType = data[0]; // 0 = image, 1 = audio
        const size =
          data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);

        if (dataType === 0) {
          // Image data header
          expectedSize = size;
          imageBuffer = Buffer.alloc(size);
          receivedSize = 0;
        } else if (dataType === 1) {
          // Audio data header
          console.log(
            `Received audio size header: ${size} bytes (dataType=${dataType})`
          ); // Keep mic logs
          audioExpectedSize = size;
          audioBuffer = Buffer.alloc(size);
          audioReceivedSize = 0;
        }
        return;
      }

      // Legacy support for 4-byte image header
      if (data.length === 4) {
        const size =
          data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
        expectedSize = size;
        imageBuffer = Buffer.alloc(size);
        receivedSize = 0;
        return;
      }

      // Handle image data
      if (imageBuffer && !audioBuffer) {
        // If we don't have a buffer yet but received data larger than 4 bytes
        // This might be image data without a preceding size header
        if (!imageBuffer && data.length > 4) {
          expectedSize = data.length;
          imageBuffer = Buffer.alloc(expectedSize);
          receivedSize = 0;
        }

        // If we still don't have a buffer, something is wrong
        if (!imageBuffer) {
          return;
        }

        // Copy data to our image buffer
        const copySize = Math.min(data.length, expectedSize - receivedSize);
        if (copySize > 0) {
          data.copy(imageBuffer, receivedSize, 0, copySize);
          receivedSize += copySize;
        }

        // Process complete image
        if (receivedSize === expectedSize && expectedSize > 0) {
          const currentTime = Date.now();
          frameCount++;
          if (currentTime - lastFrameTime >= 1000) {
            fps = frameCount;
            frameCount = 0;
            lastFrameTime = currentTime;
          }

          try {
            // Check if the data is already a JPEG (ESP32-CAM sends JPEG directly)
            const isJpeg =
              imageBuffer.length >= 4 &&
              imageBuffer[0] === 0xff &&
              imageBuffer[1] === 0xd8 &&
              (imageBuffer[imageBuffer.length - 2] === 0xff ||
                imageBuffer[imageBuffer.length - 1] === 0xd9);

            let base64Data;

            if (isJpeg) {
              base64Data = imageBuffer.toString("base64");
            } else {
              // Fallback to RGB565 conversion
              try {
                const jpegDataUrl = convertRGB565ToJPEG(imageBuffer, 640, 480);
                if (!jpegDataUrl) {
                  throw new Error("Failed to convert image");
                }
                base64Data = jpegDataUrl.split(",")[1];
              } catch (err) {
                // Just use the raw data as a last resort
                base64Data = imageBuffer.toString("base64");
              }
            }

            // Broadcast to all browser clients
            const message = JSON.stringify({
              type: "image",
              data: base64Data,
              fps: fps,
              timestamp: Date.now(),
            });

            wss.clients.forEach((client) => {
              if (!client.isESP32 && client.readyState === WebSocket.OPEN) {
                try {
                  client.send(message);
                } catch (e) {
                  clients.delete(client);
                }
              }
            });

            // Reset for next image - but keep expectedSize for debugging
            imageBuffer = null;
            receivedSize = 0;
          } catch (convError) {
            // Reset even on error
            imageBuffer = null;
            receivedSize = 0;
          }
        }
      }
      // Handle audio data
      else if (audioBuffer) {
        const copySize = Math.min(
          data.length,
          audioExpectedSize - audioReceivedSize
        );
        if (copySize > 0) {
          data.copy(audioBuffer, audioReceivedSize, 0, copySize);
          audioReceivedSize += copySize;
          console.log(
            `Received audio chunk: ${data.length} bytes, Total: ${audioReceivedSize}/${audioExpectedSize}`
          ); // Keep mic logs
        }

        // Process complete audio
        if (audioReceivedSize === audioExpectedSize && audioExpectedSize > 0) {
          console.log("Complete audio received, processing and broadcasting"); // Keep mic logs

          try {
            // Process audio data from INMP441 mic (convert from I2S format to PCM)
            const processedAudio = processAudioData(audioBuffer);

            if (!processedAudio) {
              throw new Error("Audio processing failed");
            }

            // Audio sequence for ordering in client
            audioSequence++;

            // Convert processed buffer to base64
            const base64Audio =
              processedAudio.processedBuffer.toString("base64");

            // Broadcast to all browser clients with audio metadata
            const message = JSON.stringify({
              type: "audio",
              data: base64Audio,
              timestamp: Date.now(),
              format: "pcm", // Now PCM format (converted from I2S)
              sampleRate: AUDIO_SAMPLE_RATE, // Sample rate
              bitDepth: AUDIO_BIT_DEPTH, // Now 16-bit (converted from 32-bit)
              channels: AUDIO_CHANNELS, // Mono
              sequence: audioSequence, // For ordering on client side
              maxAmplitude: processedAudio.maxAmplitude, // For visualizing audio levels
            });

            let browserClientCount = 0;
            wss.clients.forEach((client) => {
              if (!client.isESP32 && client.readyState === WebSocket.OPEN) {
                try {
                  client.send(message);
                  browserClientCount++;
                } catch (e) {
                  console.error(
                    `Error sending audio to client ${client.clientId}:`,
                    e
                  ); // Keep mic error logs
                  clients.delete(client);
                }
              }
            });

            console.log(
              `Audio broadcast to ${browserClientCount} browser clients, sequence #${audioSequence}, max amplitude: ${processedAudio.maxAmplitude}`
            ); // Keep mic logs

            // Reset for next audio packet
            audioBuffer = null;
            audioReceivedSize = 0;
          } catch (audioError) {
            console.error("Error processing audio:", audioError); // Keep mic error logs
            audioBuffer = null;
            audioReceivedSize = 0;
          }
        }
      }
    } catch (error) {
      console.error("Error processing message:", error);

      // Reset state on error
      imageBuffer = null;
      receivedSize = 0;
      audioBuffer = null;
      audioReceivedSize = 0;

      ws.send(
        JSON.stringify({ type: "error", message: "Internal server error" })
      );
    }
  });

  ws.on("close", () => {
    clearInterval(heartbeat);
    clients.delete(ws);
  });

  ws.on("error", (error) => {
    clearInterval(heartbeat);
    clients.delete(ws);
  });
});

// Add server error handling
server.on("error", (error) => {
  console.error("Server error:", error);
});

// Add process error handling
process.on("uncaughtException", (error) => {
  console.error("Uncaught Exception:", error);
});

process.on("unhandledRejection", (reason, promise) => {
  console.error("Unhandled Rejection at:", promise, "reason:", reason);
});

const PORT = process.env.PORT || 3012;
server.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
  console.log(`Audio support enabled for ESP32-CAM with INMP441 microphone`);
});
