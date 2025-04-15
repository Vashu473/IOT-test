# ESP32-S3 with Electrobot INMP441 Microphone Integration

This project demonstrates the integration of the Electrobot INMP441 MEMS Digital Microphone with an ESP32-S3-DevKitC-1 board. The microphone data is sent via WebSocket to a Node.js server and visualized in a web browser.

## Hardware Requirements

- ESP32-S3-DevKitC-1 board
- Electrobot INMP441 MEMS Digital Microphone Module
- USB cable for power and programming

## Wiring Instructions

Connect the Electrobot INMP441 microphone to the ESP32-S3 as follows:

| INMP441 Pin | ESP32-S3 Pin | Description |
|-------------|--------------|-------------|
| VDD         | 3.3V         | Power supply |
| GND         | GND          | Ground |
| SCK         | GPIO40       | Serial Clock |
| WS          | GPIO42       | Word Select (L/R Clock) |
| SD          | GPIO41       | Serial Data |
| L/R         | GND          | Channel Select (GND for left) |

## Software Setup

1. Upload the code to the ESP32-S3 using PlatformIO
2. Start the Node.js server with `node server/server.js`
3. Open a web browser and navigate to `http://localhost:3012`
4. Click "Enable Microphone" to start the audio streaming

## Features

- Real-time audio streaming from INMP441 microphone to web browser
- Audio visualization in web interface
- Built-in LED on ESP32-S3 shows audio level
- WebSocket for real-time bidirectional communication

## Troubleshooting

- If the microphone doesn't work, check the wiring connections
- Make sure the ESP32-S3 is connected to the WiFi network
- Check the WebSocket server is running
- Ensure your browser supports WebAudio API

## License

MIT 