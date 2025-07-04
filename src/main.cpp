#include <Arduino.h>
#include <list>
#include <Wifi.h>
#include <WiFiMulti.h>

#define WIFI_SSID "VoltageLoop"
#define WIFI_PASSWORD "Kirchhoff"
#define TCP_PORT 23

#define WIFI_STATUS_PIN LED_BUILTIN

#define INPUT_0 GPIO_NUM_36
#define INPUT_1 GPIO_NUM_39
#define INPUT_2 GPIO_NUM_34
#define INPUT_3 GPIO_NUM_35

IPAddress staticIP(192, 168, 1, 100);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

WiFiServer Server(TCP_PORT);
WiFiClient RemoteClient;

// Function Declarations
bool ConnectToWifi();
void ProcessTCP();
int ReadInputs();

#pragma region Setup
void setup() {
    //GPIO Setup
    
    //Status Pins
    pinMode(WIFI_STATUS_PIN, OUTPUT); 
    
    //Input Pins
    pinMode(INPUT_0, INPUT);
    pinMode(INPUT_1, INPUT);
    pinMode(INPUT_2, INPUT);
    pinMode(INPUT_3, INPUT);

    //GPIO Default Values
    digitalWrite(WIFI_STATUS_PIN, 0);


    //COM Port To Computer
    Serial.begin(921600);
    Serial.println("\n[GENERAL][ESP PROGRAM START]");

    // Wifi & Server Setup
    bool wifiConnectSuccess = ConnectToWifi();
    if (wifiConnectSuccess) 
    {
        String localIp = WiFi.localIP().toString();
        Serial.println("[TCP][LOG]: Initiating TCP server: " + WiFi.localIP().toString() + " Port: " + TCP_PORT);
        Server.begin(); 
    }
}

void loop() {
    ProcessTCP();
}

#pragma region General Functions
bool contains(const char* arr, char target) {
    for (int i = 0; i < sizeof(arr); i++) {
        if (arr[i] == target) { return true; }
    }
    return false;
}

std::vector<String> Split(String str, char delimiter) {
    std::vector<String> items = {""};
    for (int i = 0; i < str.length(); i++) {
        char thisChar = str.charAt(i);
        if (thisChar== delimiter) { items.push_back("") ;}
        else { items.back() += thisChar; }
    }
    return items;
}

std::vector<String> Split(String str, const char* delimiters) {
    std::vector<String> items = {""};
    for (int i = 0; i < str.length(); i++) {
        char thisChar = str.charAt(i);
        if (contains(delimiters, thisChar)) { items.push_back("") ;}
        else { items.back() += thisChar; }
    }
    return items;
}

#pragma region GPIO

/// @brief 
/// Reads the input pins defined in config.
/// INPUT_0 = bit 0, INPUT_1 = bit 1...
/// @return
int ReadInputs(){
    return ((digitalRead(INPUT_3) << 3) + (digitalRead(INPUT_2) << 2) + (digitalRead(INPUT_1) << 1) + digitalRead(INPUT_0));
}

#pragma region Wifi Connect
bool ConnectToWifi() {
    static String wifiName = WIFI_SSID;

    if (WiFi.status() == WL_CONNECTED) { return true; }
    if (WiFi.status() == WL_CONNECTION_LOST) { Serial.println("[WIFI][ERROR]: Lost connection to \"" + wifiName + "\""); }
    if (WiFi.status() == WL_DISCONNECTED) { Serial.println("[WIFI][ERROR]: Disconnected From \"" + wifiName + "\""); }
    digitalWrite(WIFI_STATUS_PIN, LOW);

    // Attempt to connect to WIFI up to three times.
    WiFi.config(staticIP, gateway, subnet);
    for (int i = 0; i < 3; i++) {
        Serial.println("[WIFI][LOG]: Attempting to connect to: \"" + wifiName + "\"");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        delay(500);

        if (WiFi.status() == WL_CONNECTED) { break; }
    }
    
    // Display relevant status messages and return results.
    switch (WiFi.status()) {
        case WL_CONNECTED:
            Serial.println("[WIFI][LOG]: Successfully connected to: \"" + wifiName + "\"");
            Serial.println("[WIFI][LOG]: IP: " + WiFi.localIP().toString());
            digitalWrite(WIFI_STATUS_PIN, HIGH);
            return true;
        case WL_NO_SSID_AVAIL: Serial.println("[WIFI][ERROR] Unable to find: \"" + wifiName + "\"");
        case WL_CONNECT_FAILED: Serial.println("[WIFI][ERROR]: Unable to connect to: \"" + wifiName + "\"");
        default: Serial.println("[WIFI][ERROR]: Unknown status: " + WiFi.status());
    }
    return false;
}

#pragma region Telnet Server

void InvalidCommand(String command) {
    Serial.println("[TCP][LOG]: Invalid Command Received: " + command);
    RemoteClient.println("Invalid Command");
}

void ProcessTCP() {
    static String rxBuffer = "";
    static char commandDelimiters[] = {':', ' '};
    static std::vector<String> tcpCommandsToProcess = {};

    if (WiFi.status() != WL_CONNECTED) { Serial.println("[WIFI][ERROR]: Wifi not connected, error code " + WiFi.status()); return; }

    // Handle client connecting.
    if (Server.hasClient()) { 
        // A remote client is attempting to connect.
        WiFiClient newClient = Server.available();

        if (!RemoteClient.connected()) { 
            // We do not currently have a client so accept this one.
            Serial.println("[TCP][LOG]: Connection Accepted: " + newClient.localIP().toString());
            RemoteClient = newClient;
            RemoteClient.println("ESP32 Server: Connection Accepted");
        }
        else { 
            // We already have a remote client to reject this one.
            Serial.println("[TCP][LOG]: Connection Rejected: " + newClient.localIP().toString());
            newClient.stop();
        }
    }

    // Receive any data existing in the buffer and split data into commands.
    if (RemoteClient.connected()) {
        while (RemoteClient.available() > 0) {
            char newChar = (char)RemoteClient.read();
            if (newChar == '\r') { continue; } // Don't care about '\r' chars.
            if (newChar == '\n') { 
                // Commands are delimited by '\n' char, add the command to process and clear the buffer.
                tcpCommandsToProcess.push_back(rxBuffer); 
                Serial.println("[TCP][LOG]: << " + rxBuffer);
                rxBuffer = "";
            }
            else {
                rxBuffer += newChar;
            }
        }
    }

    //Process all of the received commands.
    // GPIO:
    // |--- IN
    // |    |--- Query <"?"> -> Returns in representing input states. (Currently the only one working)
    // |
    // |--- OUT
    //      |--- Query <"?"> -> Returns int representing output states.
    //      |--- Write <int> -> Sets values according to provided integer.
    //
    while (tcpCommandsToProcess.size() != 0) {
        String command = tcpCommandsToProcess.front();
        std::vector<String> commandComponents = Split(command, commandDelimiters);
        
        if (commandComponents.at(0) == "GPIO") {
            if (commandComponents.size() == 1) { InvalidCommand(command); } // No GPIO commands of this depth.
            else if (commandComponents.at(1) == "IN?") { //Input State Query
                String inputValues = (String)ReadInputs();
                Serial.println("[TCP][LOG]: >> " + inputValues);
                RemoteClient.println(inputValues); 
            } 
            else if (commandComponents.at(1) == "OUT?") {InvalidCommand(command);} //Output State Query
            else if (commandComponents.at(1).startsWith("OUT")) {InvalidCommand(command);} // Potential Output Set
            else {InvalidCommand(command);}
        }
        else {
            InvalidCommand(command);
        }
        
        // Done processing command
        tcpCommandsToProcess.erase(tcpCommandsToProcess.begin());
    }
}