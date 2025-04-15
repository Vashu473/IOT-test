/**
 * WebSocket Server for ESP32 Audio Streaming
 * Handles both raw PCM and ADPCM audio from ESP32 devices
 */

const express = require("express");
const http = require("http");
const WebSocket = require("ws");
const path = require("path");
const fs = require("fs");
const sslRedirect = require("heroku-ssl-redirect").default;

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

// Server configuration
const app = express();
const port = process.env.PORT || 3012;
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

// Client tracking
let connectedClients = 0;
let esp32Devices = 0;
let browserClients = 0;
let audioPacketsReceived = 0;
let totalAudioPackets = 0;

// Timestamp logging function
function log(message) {
  const timestamp = new Date().toISOString();
  console.log(`[${timestamp}] ${message}`);
}

// SSL redirect middleware
app.use(sslRedirect());

// Serve static files from the public directory
app.use(express.static(path.join(__dirname, "public")));

// Home route
app.get("/", (req, res) => {
  res.sendFile(path.join(__dirname, "public", "index.html"));
});

// WebSocket connection handler
wss.on("connection", (ws, req) => {
  const clientIp = req.socket.remoteAddress;
  console.log(`New WebSocket connection from ${clientIp}`);
  connectedClients++;
  updateClientCount();

  // Send initial status to the client
  ws.send(
    JSON.stringify({
      type: "status",
      clientCount: connectedClients,
      totalAudioPackets,
    })
  );

  // Set initial state
  ws.isAlive = true;
  ws.isESP32 = false;
  ws.isBrowser = false;

  // Ping-pong heartbeat
  ws.on("pong", () => {
    ws.isAlive = true;
  });

  // Message handler
  ws.on("message", (message) => {
    try {
      // Check if message is a Buffer or string
      if (message instanceof Buffer) {
        // Binary message - forward as raw audio
        console.log(`Received binary data: ${message.length} bytes`);

        // Forward binary data to all browser clients
        wss.clients.forEach((client) => {
          if (
            client !== ws &&
            client.readyState === WebSocket.OPEN &&
            !client.isESP32
          ) {
            client.send(message);
          }
        });
        return;
      }

      // Try parsing as JSON
      const jsonData = JSON.parse(message);

      // Handle device info
      if (jsonData.type === "device_info") {
        ws.isESP32 = true;
        console.log(`Device connected: ${JSON.stringify(jsonData)}`);

        // Forward device info to all browser clients
        wss.clients.forEach((client) => {
          if (
            client !== ws &&
            client.readyState === WebSocket.OPEN &&
            !client.isESP32
          ) {
            client.send(message);
          }
        });
      }

      // Handle ESP32 JSON audio data
      else if (
        jsonData.type === "audio" &&
        jsonData.format === "pcm" &&
        Array.isArray(jsonData.data)
      ) {
        ws.isESP32 = true;
        audioPacketsReceived++;
        totalAudioPackets++;

        // Process audio data (apply noise gate etc.)
        const processedData = processAudioData(jsonData);

        // Forward to all browser clients
        const forwardingData = JSON.stringify(processedData);
        wss.clients.forEach((client) => {
          if (
            client !== ws &&
            client.readyState === WebSocket.OPEN &&
            !client.isESP32
          ) {
            client.send(forwardingData);
          }
        });

        // Log audio stats occasionally
        if (totalAudioPackets % 100 === 0) {
          console.log(
            `Audio packet #${totalAudioPackets}: ${
              jsonData.data.length
            } samples, Max: ${
              processedData.maxAmplitude
            }, RMS: ${processedData.rms.toFixed(2)}`
          );
        }
      }

      // Handle browser commands
      else if (jsonData.type === "command") {
        console.log(`Command received: ${JSON.stringify(jsonData)}`);

        // Forward commands only to ESP32 clients
        wss.clients.forEach((client) => {
          if (client.readyState === WebSocket.OPEN && client.isESP32) {
            client.send(message);
          }
        });
      }
    } catch (error) {
      console.error("Error processing message:", error.message);
    }
  });

  // Close handler
  ws.on("close", () => {
    connectedClients--;

    if (ws.isESP32) {
      esp32Devices--;
      log(`ESP32 device disconnected - Total ESP32 devices: ${esp32Devices}`);
    } else if (ws.isBrowser) {
      browserClients--;
      log(
        `Browser client disconnected - Total browser clients: ${browserClients}`
      );
    }

    log(`Client disconnected - Total clients: ${connectedClients}`);
    broadcastStatus();
  });

  // Error handler
  ws.on("error", (error) => {
    log(`WebSocket error: ${error.message}`);
  });
});

// Broadcast server status to all clients
function broadcastStatus() {
  const statusMessage = JSON.stringify({
    type: "status",
    clients: connectedClients,
    esp32Devices: esp32Devices,
    browserClients: browserClients,
    audioPackets: totalAudioPackets,
  });

  wss.clients.forEach((client) => {
    if (client.readyState === WebSocket.OPEN) {
      client.send(statusMessage);
    }
  });
}

// Process audio data (apply noise gate and forward to clients)
function processAudioData(audioData) {
  // Convert to Int16Array for processing
  const samples = new Int16Array(audioData.data);
  const sampleCount = samples.length;

  // Calculate audio statistics
  let maxAmplitude = 0;
  let sumSquares = 0;

  for (let i = 0; i < sampleCount; i++) {
    const absValue = Math.abs(samples[i]);
    if (absValue > maxAmplitude) {
      maxAmplitude = absValue;
    }
    sumSquares += samples[i] * samples[i];
  }

  // Calculate RMS (Root Mean Square)
  const rms = Math.sqrt(sumSquares / sampleCount);

  // Apply dynamic noise gate
  // - Use a lower threshold for higher RMS values
  // - Use a higher threshold for lower RMS values
  const baseThreshold = 200;
  const dynamicThreshold = baseThreshold * (1 - Math.min(1, rms / 5000));
  const noiseGateThreshold = Math.max(50, dynamicThreshold);

  // Apply noise gate
  const processedSamples = applyNoiseGate(samples, noiseGateThreshold);

  // Return processed data
  return {
    type: "audio",
    format: "pcm",
    sampleRate: audioData.sampleRate,
    data: Array.from(processedSamples),
    maxAmplitude: maxAmplitude,
    rms: rms,
    threshold: noiseGateThreshold,
  };
}

// Apply noise gate to audio samples
function applyNoiseGate(samples, threshold) {
  const processedSamples = new Int16Array(samples.length);

  for (let i = 0; i < samples.length; i++) {
    if (Math.abs(samples[i]) < threshold) {
      processedSamples[i] = 0; // Zero out low amplitude samples
    } else {
      processedSamples[i] = samples[i]; // Keep higher amplitude samples
    }
  }

  return processedSamples;
}

// Heartbeat to check for dead connections
const intervalId = setInterval(() => {
  wss.clients.forEach((ws) => {
    if (ws.isAlive === false) {
      log("Terminating inactive connection");
      return ws.terminate();
    }

    ws.isAlive = false;
    ws.ping(() => {});
  });
}, 30000);

// Clean up interval on server close
wss.on("close", () => {
  clearInterval(intervalId);
});

// Start the server
server.listen(port, () => {
  log(`Server started on port ${port}`);
});
