#include <SPI.h>
#include <MFRC522.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

// Pin configuration for MFRC522
#define RST_PIN 25 // Reset pin
#define SS_PIN 26  // Slave Select (SDA) pin

MFRC522 rfid(SS_PIN, RST_PIN); // Create an MFRC522 instance

// MAC address of the receiver ESP32 (replace with your receiver's MAC address)
uint8_t receiverMAC[] = {0xC8, 0x2E, 0x18, 0xF7, 0x71, 0x44}; 

 
// The Wi-Fi channel of the specified SSID
uint8_t channel = 1;

// LED pins for status indication
const uint8_t GREEN_LED_PIN = 27;
const uint8_t RED_LED_PIN = 14;
const uint8_t Buzzer = 12;

// Structure for sending messages
typedef struct struct_message {
    char msg[32]; // Message buffer to store UID or access status
} struct_message;

struct_message myData;

// Callback when a message is sent via ESP-NOW
void onSent(const uint8_t *macAddr, esp_now_send_status_t status) {
    Serial.print("Message delivery status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// Callback to handle messages received via ESP-NOW
void onReceive(const esp_now_recv_info *recv_info, const uint8_t *data, int len) {
    struct_message receivedData;
    memcpy(&receivedData, data, sizeof(receivedData));
    String message = String(receivedData.msg);
    
    if (message == "access_granted") {
      buzzer_signal();
      digitalWrite(RED_LED_PIN, LOW);
      digitalWrite(GREEN_LED_PIN, HIGH);
      delay(1000); 
      digitalWrite(GREEN_LED_PIN, LOW);
      digitalWrite(RED_LED_PIN, HIGH);
    } else if (message == "access_denied") {
      red_led_buzzer(); 
    }
}

// Function to blink the red LED a specified number of times
void red_led_buzzer() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(RED_LED_PIN, HIGH);
        digitalWrite(Buzzer, HIGH);
        delay(500);
        digitalWrite(RED_LED_PIN, LOW);
        digitalWrite(Buzzer, LOW);
        delay(500);
    }
}

void buzzer_signal(){
  for (int i = 0; i < 2; i++) {
        digitalWrite(Buzzer, HIGH);
        delay(100);
        digitalWrite(Buzzer, LOW);
        delay(100);

    }
}

void setup() {
    Serial.begin(115200);
    
    // Setup LED pins
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(Buzzer, OUTPUT);

    digitalWrite(RED_LED_PIN, HIGH); // Start with red LED on (access denied)
    digitalWrite(GREEN_LED_PIN, LOW);

    // Initialize Wi-Fi in station mode
    WiFi.mode(WIFI_STA);

    // Set Wi-Fi channel for ESP-NOW communication
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    // Register callback functions for sending and receiving messages
    esp_now_register_send_cb(onSent);
    esp_now_register_recv_cb(onReceive);

    // Add the receiver as a peer
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMAC, 6);
    peerInfo.channel = channel;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer!");
        return;
    }

    // Initialize SPI bus for RFID reader
    SPI.begin();
    rfid.PCD_Init();
    Serial.println("RFID reader initialized.");
}

void loop() {
    // Check for new RFID card
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        // Convert the UID to a string
        String uidString = "";
        for (byte i = 0; i < rfid.uid.size; i++) {
            uidString += String(rfid.uid.uidByte[i], HEX);
            if (i < rfid.uid.size - 1) {
                uidString += ":"; // Separate bytes with a colon
            }
        }

        // Store UID in the message struct
        strncpy(myData.msg, uidString.c_str(), sizeof(myData.msg) - 1);
        myData.msg[sizeof(myData.msg) - 1] = '\0'; // Null-terminate to prevent buffer overflow

        Serial.print("Card UID: ");
        Serial.println(myData.msg);

        // Send UID to the receiver ESP32 via ESP-NOW
        esp_err_t result = esp_now_send(receiverMAC, (uint8_t *)&myData, sizeof(myData));
        if (result == ESP_OK) {
            Serial.println("Message sent successfully");
        } else {
            Serial.println("Error sending message");
        }

        // Halt the card to avoid re-reading
        rfid.PICC_HaltA();
    }

    delay(100); // Short delay to prevent rapid card scanning
}
