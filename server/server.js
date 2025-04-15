/**
 * WebSocket Server for ESP32 Audio Streaming
 * Handles both raw PCM and ADPCM audio from ESP32 devices
 */

const express = require("express");
const http = require("http");
const WebSocket = require("ws");
const path = require("path");

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
let packetCount = 0;

// ESP32 audio packet constants
const PACKET_HEADER_MAGIC = 0xa5;
const PACKET_TYPE_AUDIO = 0x01;
const PACKET_HEADER_SIZE = 8;

// Timestamp logging function
function log(message) {
  const timestamp = new Date().toISOString();
  console.log(`[${timestamp}] ${message}`);
}

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

  // Start with unidentified client
  ws.isESP32 = false;
  ws.isBrowser = false;

  // Log the connection headers
  console.log(`Connection headers: ${JSON.stringify(req.headers)}`);

  // Try to identify browser clients by headers
  const userAgent = req.headers["user-agent"] || "";
  if (
    userAgent.includes("Mozilla") ||
    userAgent.includes("Chrome") ||
    userAgent.includes("Safari")
  ) {
    ws.isBrowser = true;
    browserClients++;
    console.log(`Browser client auto-identified by User-Agent: ${userAgent}`);
  }

  broadcastStatus();

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

  // Ping-pong heartbeat
  ws.on("pong", () => {
    ws.isAlive = true;
  });

  // Message handler
  ws.on("message", (message) => {
    try {
      // Binary message handling
      if (message instanceof Buffer) {
        // Process binary data from ESP32
        processAudioData(ws, message);
        return;
      }

      // If not binary, parse as JSON message
      try {
        const data = JSON.parse(message);

        // Handle client identification
        if (data.type === "hello" && data.client === "browser") {
          ws.isBrowser = true;
          browserClients++;
          log(
            `Browser client identified - Total browser clients: ${browserClients}`
          );
        }

        // Handle ESP32 identification
        if (data.type === "hello" && data.client === "esp32") {
          ws.isESP32 = true;
          esp32Devices++;
          log(`ESP32 device identified - Total ESP32 devices: ${esp32Devices}`);
        }

        // Handle commands from browser to ESP32
        if (data.type === "command") {
          // Forward mic control commands to ESP32 devices
          if (data.action === "mic_on" || data.action === "mic_off") {
            log(`Forwarding command: ${data.action}`);

            // Format command in the way ESP32 expects
            const command = JSON.stringify({
              command: data.action,
            });

            // Send to all ESP32 clients
            wss.clients.forEach((client) => {
              if (client.isESP32 && client.readyState === WebSocket.OPEN) {
                client.send(command);
              }
            });
          }
        }
      } catch (jsonError) {
        console.error("Invalid JSON message:", jsonError.message);
      }
    } catch (error) {
      console.error(`Error processing message: ${error.message}`);
      console.error(error.stack);
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
function processAudioData(ws, data) {
  try {
    // Safety check - minimum packet size for our new format
    if (!data || data.length < PACKET_HEADER_SIZE) {
      console.warn(
        `Received invalid audio packet (too small): ${
          data ? data.length : 0
        } bytes`
      );
      return;
    }

    // Check magic byte and packet type
    if (data[0] !== PACKET_HEADER_MAGIC || data[1] !== PACKET_TYPE_AUDIO) {
      console.log(
        `Ignoring non-standard packet: Magic=${data[0]}, Type=${data[1]}, expected Magic=${PACKET_HEADER_MAGIC}, Type=${PACKET_TYPE_AUDIO}`
      );
      return;
    }

    // Parse the header
    const seqNum = (data[2] << 8) | data[3];
    const numSamples = (data[4] << 8) | data[5];
    const checksum = (data[6] << 8) | data[7];

    console.log(
      `Received audio packet #${seqNum}: ${numSamples} samples, checksum: ${checksum}`
    );

    // Calculate expected data size
    const dataSizeBytes = numSamples * 2; // 16-bit samples = 2 bytes each

    // Validate packet size
    if (data.length < PACKET_HEADER_SIZE + dataSizeBytes) {
      console.warn(
        `Audio packet too small: ${data.length} bytes, expected ${
          PACKET_HEADER_SIZE + dataSizeBytes
        }`
      );
      return;
    }

    // Verify checksum
    let calculatedChecksum = 0;
    for (let i = 0; i < numSamples; i++) {
      const offset = PACKET_HEADER_SIZE + i * 2;
      const sample = data.readInt16BE(offset); // Read as big-endian
      calculatedChecksum += Math.abs(sample);
    }

    // If checksums don't match (roughly), log but still continue
    if (Math.abs(calculatedChecksum - checksum) > numSamples) {
      console.warn(
        `Checksum mismatch: received=${checksum}, calculated=${calculatedChecksum}`
      );
    }

    // Forward to all clients
    let sentCount = 0;
    wss.clients.forEach((client) => {
      if (client !== ws && client.readyState === WebSocket.OPEN) {
        try {
          client.send(data);
          sentCount++;
        } catch (err) {
          console.error(`Error sending audio to client: ${err.message}`);
        }
      }
    });

    console.log(`Forwarded audio packet #${seqNum} to ${sentCount} clients`);

    // Log statistics occasionally
    totalAudioPackets++;
    if (totalAudioPackets % 100 === 0) {
      console.log(`Total audio packets processed: ${totalAudioPackets}`);
    }
  } catch (error) {
    console.error(`Error processing audio data: ${error.message}`);
    console.error(error.stack);
  }
}

// Helper function to send binary data to all clients
function sendToAllClients(data, excludeClient = null) {
  let count = 0;
  wss.clients.forEach((client) => {
    if (client !== excludeClient && client.readyState === WebSocket.OPEN) {
      client.send(data);
      count++;
    }
  });
  return count;
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
