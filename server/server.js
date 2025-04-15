/**
 * WebSocket Server for ESP32 Audio Streaming
 * Handles both raw PCM and ADPCM audio from ESP32 devices
 */

const express = require("express");
const http = require("http");
const WebSocket = require("ws");
const fs = require("fs");
const path = require("path");

/**
 * ADPCM Decoder - Decodes ADPCM audio streams from ESP32 clients
 */
class ADPCMDecoder {
  constructor() {
    // ADPCM step size table
    this.stepTable = [
      7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
      45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
      209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
      876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
      2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
      8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
      24623, 27086, 29794, 32767,
    ];

    // ADPCM index table
    this.indexTable = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

    // Reset the decoder state
    this.reset();
  }

  /**
   * Reset decoder state
   */
  reset() {
    this.valprev = 0; // Previous output value
    this.index = 0; // Index into step size table
  }

  /**
   * Decode a single 4-bit ADPCM code to a 16-bit PCM sample
   *
   * @param {number} code - 4-bit ADPCM code (0-15)
   * @returns {number} - 16-bit PCM sample
   */
  decodeSample(code) {
    // Get step size from table
    const step = this.stepTable[this.index];

    // Compute difference
    let diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;

    // Add or subtract from previous value
    if (code & 8) {
      this.valprev -= diff;
    } else {
      this.valprev += diff;
    }

    // Clamp to 16-bit range
    this.valprev = Math.max(-32768, Math.min(32767, this.valprev));

    // Update index value
    this.index += this.indexTable[code];
    this.index = Math.max(0, Math.min(88, this.index));

    return this.valprev;
  }

  /**
   * Decode ADPCM buffer to PCM
   *
   * @param {Buffer} adpcmData - Buffer containing ADPCM data
   * @returns {Int16Array} - PCM samples
   */
  decode(adpcmData) {
    const pcmSamples = new Int16Array(adpcmData.length * 2);

    // Reset decoder state before decoding a new buffer
    this.reset();

    for (let i = 0; i < adpcmData.length; i++) {
      // Each byte contains two 4-bit ADPCM codes
      const byte = adpcmData[i];

      // High nibble (first sample)
      const code1 = (byte >> 4) & 0x0f;
      pcmSamples[i * 2] = this.decodeSample(code1);

      // Low nibble (second sample)
      const code2 = byte & 0x0f;
      pcmSamples[i * 2 + 1] = this.decodeSample(code2);
    }

    return pcmSamples;
  }

  /**
   * Process incoming ESP32 audio packet
   *
   * @param {Buffer} data - Binary data from WebSocket
   * @returns {Object} - Object containing decoded audio data
   */
  static processPacket(data) {
    if (data.length < 6) {
      throw new Error("Invalid packet size");
    }

    // Parse header
    const packetType = data[0];
    const dataSize =
      data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
    const format = data[5]; // 0 = raw PCM, 1 = ADPCM

    if (packetType !== 1) {
      throw new Error("Not an audio packet");
    }

    // Extract the audio data (skip the 6-byte header)
    const audioData = data.slice(6, 6 + dataSize);

    return {
      type: packetType,
      format: format,
      size: dataSize,
      data: audioData,
    };
  }
}

// Configuration
const PORT = 3012;
const HTTP_PORT = PORT;
const WS_PORT = PORT;

// Create Express app and HTTP server
const app = express();
const server = http.createServer(app);

// Create WebSocket server
const wss = new WebSocket.Server({ server });

// Serve static files
app.use(express.static(path.join(__dirname, "public")));

// Stats
let connectedClients = 0;
let totalBytesReceived = 0;
let totalAudioPackets = 0;

// Create audio output directory if it doesn't exist
const audioDir = path.join(__dirname, "audio");
if (!fs.existsSync(audioDir)) {
  fs.mkdirSync(audioDir);
}

// WebSocket connection handler
wss.on("connection", (ws, req) => {
  const clientIP = req.socket.remoteAddress;
  connectedClients++;

  console.log(
    `[${new Date().toISOString()}] New client connected: ${clientIP} (Total: ${connectedClients})`
  );

  // Create ADPCM decoder for this client
  const decoder = new ADPCMDecoder();

  // Track client type
  ws.isESP32 = false;

  // Variables for this connection
  let isRecording = false;
  let clientBytesReceived = 0;
  let clientAudioPackets = 0;
  let recordingPath = null;
  let recordingStream = null;

  // Send initial state to client
  ws.send(
    JSON.stringify({
      type: "status",
      connected: true,
      clients: connectedClients,
    })
  );

  // Message handler
  ws.on("message", (message) => {
    try {
      if (typeof message === "string") {
        try {
          // Try parsing as JSON (for ESP32 JSON messages)
          const jsonData = JSON.parse(message);

          // Handle ESP32 JSON audio data
          if (
            jsonData.type === "audio" &&
            jsonData.format === "pcm" &&
            Array.isArray(jsonData.data)
          ) {
            // Mark this client as ESP32
            ws.isESP32 = true;
            clientAudioPackets++;
            totalAudioPackets++;

            // Extract PCM data
            const pcmData = jsonData.data;
            const sampleRate = jsonData.sampleRate || 44100;

            // Forward to all browser clients
            wss.clients.forEach((client) => {
              if (
                client !== ws &&
                client.readyState === WebSocket.OPEN &&
                !client.isESP32
              ) {
                client.send(message); // Forward as-is
              }
            });

            return;
          }

          // Handle command JSON
          if (jsonData.type === "command") {
            if (jsonData.action === "mic_on") {
              console.log(
                `[${new Date().toISOString()}] Sending mic_on command`
              );
              // Forward to all ESP32 clients
              wss.clients.forEach((client) => {
                if (client.isESP32 && client.readyState === WebSocket.OPEN) {
                  client.send("mic_on");
                }
              });
            } else if (jsonData.action === "mic_off") {
              console.log(
                `[${new Date().toISOString()}] Sending mic_off command`
              );
              // Forward to all ESP32 clients
              wss.clients.forEach((client) => {
                if (client.isESP32 && client.readyState === WebSocket.OPEN) {
                  client.send("mic_off");
                }
              });
            }
          }
        } catch (e) {
          // Not JSON, try as plain text commands
          if (
            message === "mic_on" ||
            message === "mic_off" ||
            message === "mic_check"
          ) {
            // Forward simple commands to all ESP32 clients
            wss.clients.forEach((client) => {
              if (client.isESP32 && client.readyState === WebSocket.OPEN) {
                client.send(message);
              }
            });
          }
        }
      } else if (Buffer.isBuffer(message) || message instanceof ArrayBuffer) {
        // Mark this client as ESP32
        ws.isESP32 = true;

        // Handle binary messages (audio data)
        const buffer = Buffer.from(message);
        clientBytesReceived += buffer.length;
        totalBytesReceived += buffer.length;

        try {
          // Process the audio packet
          const packet = ADPCMDecoder.processPacket(buffer);

          if (packet.type === 1) {
            // Audio packet
            clientAudioPackets++;
            totalAudioPackets++;

            // Validate data
            if (!packet.data || packet.data.length === 0) {
              console.error(
                `[${new Date().toISOString()}] Empty audio data in packet`
              );
              return;
            }

            let pcmSamples;
            if (packet.format === 1) {
              // ADPCM
              pcmSamples = decoder.decode(packet.data);
            } else {
              // Raw PCM
              pcmSamples = new Int16Array(
                packet.data.buffer,
                packet.data.byteOffset,
                packet.data.length / 2
              );
            }

            // Check if we have any non-zero audio samples
            let hasNonZeroSamples = false;
            let maxSample = 0;
            for (let i = 0; i < Math.min(pcmSamples.length, 1000); i++) {
              if (pcmSamples[i] !== 0) {
                hasNonZeroSamples = true;
                maxSample = Math.max(maxSample, Math.abs(pcmSamples[i]));
              }
            }

            // Forward to all web clients if non-zero
            if (hasNonZeroSamples) {
              wss.clients.forEach((client) => {
                if (
                  client !== ws &&
                  client.readyState === WebSocket.OPEN &&
                  !client.isESP32
                ) {
                  const dataToSend = {
                    type: "audio",
                    format: "pcm",
                    sampleRate: 44100,
                    channels: 1,
                    bitsPerSample: 16,
                    data: Array.from(
                      pcmSamples.slice(0, Math.min(pcmSamples.length, 1000))
                    ), // Limit array size
                  };
                  client.send(JSON.stringify(dataToSend));
                }
              });
            }
          }
        } catch (e) {
          // If packet processing fails, might be raw audio data without header
          console.error(
            `[${new Date().toISOString()}] Error processing packet: ${
              e.message
            }`
          );
        }
      }
    } catch (e) {
      console.error(
        `[${new Date().toISOString()}] Error handling message: ${e.message}`
      );
    }
  });

  // Close handler
  ws.on("close", () => {
    connectedClients--;

    // Stop recording if active
    if (isRecording && recordingStream) {
      recordingStream.end();
      console.log(
        `[${new Date().toISOString()}] Stopped recording due to client disconnect`
      );
    }

    console.log(
      `[${new Date().toISOString()}] Client disconnected: ${clientIP} (Total: ${connectedClients})`
    );

    // Notify other clients
    wss.clients.forEach((client) => {
      if (client !== ws && client.readyState === WebSocket.OPEN) {
        client.send(
          JSON.stringify({
            type: "status",
            clients: connectedClients,
          })
        );
      }
    });
  });
});

// Start server
server.listen(HTTP_PORT, () => {
  const serverStartTime = new Date().toISOString();
  console.log(`[${serverStartTime}] ================================`);
  console.log(`[${serverStartTime}] ESP32 Audio Server Started`);
  console.log(`[${serverStartTime}] HTTP server running on port ${HTTP_PORT}`);
  console.log(
    `[${serverStartTime}] Open http://localhost:${HTTP_PORT} in your browser`
  );
  console.log(`[${serverStartTime}] ================================`);
});
