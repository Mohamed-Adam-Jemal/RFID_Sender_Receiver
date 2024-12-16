#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <ArduinoJson.h>

// ** Wi-Fi Credentials **
const char* ssid = "baga";
const char* password = "11111111";

// ** Server endpoints **
const char* getUsersEndpoint = "http://192.168.118.99:8000/api/get-users/";
const char* logAccessEndpoint = "http://192.168.118.99:8000/api/add-access-log/";

// ** Structure to hold the received message (ESP-NOW) **
typedef struct struct_message {
    char msg[32];
} struct_message;

struct_message sendData;
struct_message receivedData;
char received_rfid_tag[32] = "";

uint8_t senderMac[] = {0x30, 0xAE, 0xA4, 0xFE, 0x33, 0x34};

// ** Chained list structure for local storage of RFID tags **
typedef struct Node {
    char rfidTag[32];
    Node* next;
} Node;

Node* head = nullptr; // Head of the linked list

// ** LED Pins **
const int locker = 27; 

// ** Function to add RFID tag to linked list (avoid duplicates) **
void addTagToList(const char* rfidTag) {
    Node* current = head;
    while (current != nullptr) {
        if (strcmp(current->rfidTag, rfidTag) == 0) {
            return; // RFID already in the list
        }
        current = current->next;
    }

    // Add a new tag to the front of the list
    Node* newNode = new Node;
    strcpy(newNode->rfidTag, rfidTag);
    newNode->next = head;
    head = newNode;
}

// ** Function to check if an RFID tag exists in the linked list **
bool isTagInList(const char* rfidTag) {
    Node* current = head;
    while (current != nullptr) {
        if (strcmp(current->rfidTag, rfidTag) == 0) {
            return true; // Tag found
        }
        current = current->next;
    }
    return false; // Tag not found
}

// ** ESP-NOW receive callback **
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != sizeof(struct_message)) {
        Serial.println("Invalid message length!");
        return;
    }
    memcpy(&receivedData, data, sizeof(receivedData));
    strcpy(received_rfid_tag, receivedData.msg);
    Serial.print("Received RFID Tag: ");
    Serial.println(received_rfid_tag);
}

// ** ESP-NOW send callback (only confirms sending) **
void onSent(const uint8_t *macAddr, esp_now_send_status_t status) {
    Serial.print("ESP-NOW message sent with status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// ** Function to send ESP-NOW messages **
void sendFeedbackMsg(const char* message) {
    strcpy(sendData.msg, message);
    esp_now_send(senderMac, (uint8_t*)&sendData, sizeof(sendData));
    Serial.print("Sent message: ");
    Serial.println(sendData.msg);
}

// ** Function to fetch users from the server and update the local list **
void fetchUsersFromServer() {
    HTTPClient http;
    http.begin(getUsersEndpoint);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
        String payload = http.getString();
        Serial.println("Users list is updated from the server.");
        
        DynamicJsonDocument doc(2048); 
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
          Node* current = head;
          while (current != nullptr) {
              Node* temp = current;
              current = current->next;
              delete temp;
          }
          head = nullptr; 

          for (JsonObject obj : doc.as<JsonArray>()) {
            const char* rfidTag = obj["rfid_tag"];
            if (rfidTag) {
              addTagToList(rfidTag);
              Serial.print("Added RFID tag to list: ");
              Serial.println(rfidTag);
            }
          }
        } else {
            Serial.print("JSON deserialization error: ");
            Serial.println(error.f_str());
        }
    } else {
        Serial.print("Error on HTTP GET request: ");
        Serial.println(httpResponseCode);
    }

    http.end();
}

// ** Function to POST an access log to the server **
void postAccessLog(const char* rfidTag) {
    if (strlen(rfidTag) > 0) {
        HTTPClient http;
        http.begin(logAccessEndpoint);
        http.addHeader("Content-Type", "application/json");

        DynamicJsonDocument doc(256);
        doc["rfid_tag"] = rfidTag;

        String jsonPayload;
        serializeJson(doc, jsonPayload);

        int httpResponseCode = http.POST(jsonPayload);

        if (httpResponseCode > 0) {
            Serial.print("Server response: ");
            Serial.println(http.getString());
        } else {
            Serial.print("Error on HTTP POST request: ");
            Serial.println(httpResponseCode);
        }

        http.end();
    }
}

void setup() {
  Serial.begin(115200);
  pinMode(locker, OUTPUT);
  digitalWrite(locker, LOW);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);

  Serial.println("Connecting to Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.print(".");
  }

  Serial.println("\nConnected to Wi-Fi!");
  Serial.print("Station IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.macAddress());
  Serial.print("Wi-Fi Channel: ");
  Serial.println(WiFi.channel());
  if (esp_now_init() != ESP_OK) {
      Serial.println("ESP-NOW Initialization Failed!");
      while (true);
  }

  esp_now_register_recv_cb(onReceive);
  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, senderMac, 6);
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add peer!");
    }

  fetchUsersFromServer();
}

void loop() {
    static unsigned long lastFetchTime = 0;

    if (strlen(received_rfid_tag) > 0) {
        bool accessGranted = isTagInList(received_rfid_tag);
        if (accessGranted) {
            Serial.println("Access Granted! Tag is in the list.");
            sendFeedbackMsg("access_granted");
            digitalWrite(locker, HIGH);
            delay(1000);
            digitalWrite(locker, LOW);
            postAccessLog(received_rfid_tag);
        } else {
            Serial.println("Access Denied! Tag not in the list.");
            sendFeedbackMsg("access_denied");
            postAccessLog(received_rfid_tag);
        }
        digitalWrite(locker, LOW);
        strcpy(received_rfid_tag, ""); 
    }

    if (millis() - lastFetchTime > 10000) {
        fetchUsersFromServer();
        lastFetchTime = millis();
    }

    delay(500);
}
