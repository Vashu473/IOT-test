/*
Level 1: WiFi Auto-Connect with Multiple Passwords
Description: Ye program available WiFi networks ko scan karta hai aur predefined passwords
            se connect hone ki koshish karta hai.
Author: Your Name
Date: Current Date
*/

#include <Arduino.h> // Arduino ke basic functions ke liye
#include <WiFi.h>    // WiFi functions ke liye

// Passwords ki list - in passwords se WiFi connect karne ki koshish karenge
const char *passwords[] = {
    "vashudev",       // Password 1
    "ashish20032300", // Password 2
    "Aa@20032300"     // Password 3
};
const int NUM_PASSWORDS = 3; // Total passwords ki count

// ESP32-CAM ka built-in LED pin - isse hum status indicate karenge
const int LED_BUILTIN = 33; // GPIO 33 pin LED ke liye use karenge

// WiFi ke different status codes ko human readable format me convert karne ka function
void printWiFiStatus(wl_status_t status)
{
    switch (status)
    {
    case WL_IDLE_STATUS: // WiFi idle hai
        Serial.println("WiFi IDLE status - WiFi module kaam kar raha hai par connected nahi hai");
        break;
    case WL_NO_SSID_AVAIL: // Koi WiFi network nahi mila
        Serial.println("WiFi network nahi mil raha hai - Check karo ki network range me hai");
        break;
    case WL_SCAN_COMPLETED: // WiFi scan complete ho gaya
        Serial.println("WiFi scan complete ho gaya - Networks mil gaye hain");
        break;
    case WL_CONNECTED: // WiFi connected ho gaya
        Serial.println("WiFi connected! Network se successfully jud gaye hain");
        break;
    case WL_CONNECT_FAILED: // Connection fail ho gaya
        Serial.println("WiFi connection fail ho gaya - Password galat ho sakta hai");
        break;
    case WL_CONNECTION_LOST: // Connection lost ho gaya
        Serial.println("WiFi connection lost ho gaya - Signal weak ho sakta hai");
        break;
    case WL_DISCONNECTED: // Disconnected ho gaya
        Serial.println("WiFi disconnected hai - Connection toot gaya hai");
        break;
    default: // Koi aur status code
        Serial.printf("Unknown WiFi status: %d - Ye status code samajh me nahi aaya\n", status);
        break;
    }
}

// WiFi se connect karne ka function - ye ek network aur password ke saath connection try karta hai
bool tryConnectWiFi(const char *ssid, const char *password)
{
    Serial.printf("\nNetwork '%s' ke saath password '%s' try kar rahe hain\n", ssid, password);

    // WiFi connection start karte hain
    WiFi.begin(ssid, password);

    // 10 seconds (20 attempts * 500ms) tak wait karenge connection ke liye
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20)
    {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // LED ko blink karte hain
        delay(500);                                           // Half second wait
        Serial.print(".");                                    // Progress dikhane ke liye dots print karte hain
        attempts++;
    }

    // Check karte hain ki connection successful hua ya nahi
    if (WiFi.status() == WL_CONNECTED)
    {
        digitalWrite(LED_BUILTIN, LOW); // LED on kar dete hain success dikhane ke liye
        Serial.println("\nConnection successful ho gaya!");
        Serial.printf("Network: %s\n", ssid);
        Serial.printf("Password: %s\n", password);
        Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("Signal Strength: %d dBm\n", WiFi.RSSI());
        return true; // Connection successful
    }

    WiFi.disconnect(); // Agar connection fail hua to disconnect kar dete hain
    return false;      // Connection failed
}

void setup()
{
    // LED pin ko output mode me set karte hain
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // LED off karte hain shuru me

    // Serial communication start karte hain debugging ke liye
    Serial.begin(115200); // 115200 baud rate set karte hain
    delay(2000);          // 2 second wait karte hain serial start hone ke liye

    // Program start hone ka message print karte hain
    Serial.println("\n\n=================================");
    Serial.println("ESP32 WiFi Auto-Connect Test Level 1");
    Serial.println("=================================");

    // WiFi ko station mode me set karte hain (means WiFi network se connect hone ke liye)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true); // Pehle disconnect kar dete hain agar connected ho
    delay(1000);           // 1 second wait

    bool connected = false; // Connection status track karne ke liye variable

    // Jab tak successful connection nahi hota, tab tak try karte rahenge
    while (!connected)
    {
        // Available WiFi networks scan karte hain
        Serial.println("\nWiFi networks scan kar rahe hain...");
        int numNetworks = WiFi.scanNetworks();

        // Agar koi network nahi mila
        if (numNetworks == 0)
        {
            Serial.println("Koi WiFi network nahi mila!");
            delay(5000); // 5 second wait karke dobara try karenge
            continue;
        }

        // Mile hue networks ki list print karte hain
        Serial.printf("\n%d networks mile hain:\n", numNetworks);

        // Har ek network ke liye try karte hain
        for (int i = 0; i < numNetworks && !connected; i++)
        {
            String ssid = WiFi.SSID(i); // Network ka naam
            int rssi = WiFi.RSSI(i);    // Signal strength

            Serial.printf("\n%d. Testing network: %s (Signal Strength: %d dBm)\n", i + 1, ssid.c_str(), rssi);

            // Har ek password try karte hain is network ke saath
            for (int j = 0; j < NUM_PASSWORDS && !connected; j++)
            {
                connected = tryConnectWiFi(ssid.c_str(), passwords[j]);
                if (!connected)
                {
                    Serial.printf("Password '%s' fail ho gaya network '%s' ke liye\n", passwords[j], ssid.c_str());
                }
            }
        }

        WiFi.scanDelete(); // Scan results ko memory se clear karte hain

        // Agar abhi bhi connected nahi hue
        if (!connected)
        {
            Serial.println("\nKisi bhi network se connect nahi ho paye.");
            Serial.println("5 seconds me dobara try karenge...");
            delay(5000);
        }
    }
}

void loop()
{
    // Connected hai to status check karte rahenge
    if (WiFi.status() == WL_CONNECTED)
    {
        digitalWrite(LED_BUILTIN, LOW); // LED on rakhenge jab connected ho
        Serial.printf("Connected chal raha hai | Signal: %d dBm | IP: %s\n",
                      WiFi.RSSI(),
                      WiFi.localIP().toString().c_str());
    }
    else
    {
        digitalWrite(LED_BUILTIN, HIGH); // LED off karenge jab disconnected ho
        Serial.println("Connection lost ho gaya! ESP32 ko restart kar rahe hain...");
        ESP.restart(); // ESP32 ko restart karte hain agar connection lost ho jaye
    }
    delay(5000); // 5 second wait karte hain next check ke liye
}