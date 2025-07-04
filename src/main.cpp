#include <Arduino.h>
#include <list>
#include <string>
#include <cmath>
#include <Wifi.h>
#include <WiFiMulti.h>
#include "esp_efuse.h"
#include "driver/ledc.h"

#define WIFI_SSID "VoltageLoop"
#define WIFI_PASSWORD "Kirchhoff"
#define TCP_PORT 23

#define WIFI_STATUS_PIN LED_BUILTIN

#define INPUT_0 GPIO_NUM_36 
#define INPUT_1 GPIO_NUM_39 
#define INPUT_2 GPIO_NUM_34 
#define INPUT_3 GPIO_NUM_35 

#define OUTPUT_0_PIN GPIO_NUM_23 // Used for mapping PWM modules to outputs.
#define OUTPUT_1_PIN GPIO_NUM_22 // ^^
#define OUTPUT_2_PIN GPIO_NUM_21 // ^

#define OUTPUT_0_PWM LEDC_CHANNEL_0 // Used to write duty cycle to PWM modules / outputs.
#define OUTPUT_1_PWM LEDC_CHANNEL_1 // ^^ 
#define OUTPUT_2_PWM LEDC_CHANNEL_2 // ^

#define GPIO_PWM_FREQUENCY 10e3
#define GPIO_PWM_RESOLUTION 8

int GPIO_PWM_MAX_COUNT = pow(2, GPIO_PWM_RESOLUTION) - 1;
int GPIO_PWM_DUTY_CYCLES[] = {0, 0, 0};

IPAddress staticIP(192, 168, 1, 100);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

WiFiServer Server(TCP_PORT);
WiFiClient RemoteClient;

String ID_STRING = "ESP32_TCP_SERVER;V0.0.1;ESP0001"; //Name;FW Version;SN

String TCP_COMMAND_TREE = 
"Commands:                          |\n"
"HELP?                              | string: This Prompt.\n"
"*IDN? or ID?                       | Returns id string.\n"
"GPIO:                              |\n"                           
"|---IN?                            | int: integer rep of inputs, IN0 = LSB.\n"
"|---OUT(<channel number>)?         | float: Output duty cycle [0-1].\n"
"|   OUT(<channel number>) <value>  | float: Sets output duty cycle to <value>\n";

// Function Declarations
bool ConnectToWifi();
void ProcessTCP();
void WriteToClient();
int ReadInputs();

#pragma region Setup
void setup() {
    //GPIO Setup
    
    //PWM Setup
    ledcSetup(OUTPUT_0_PWM, GPIO_PWM_FREQUENCY, GPIO_PWM_RESOLUTION); // GPIO Ouput PWM Controllers
    ledcSetup(OUTPUT_1_PWM, GPIO_PWM_FREQUENCY, GPIO_PWM_RESOLUTION); // ^^
    ledcSetup(OUTPUT_2_PWM, GPIO_PWM_FREQUENCY, GPIO_PWM_RESOLUTION); // ^

    ledcSetup(4, 50, 16); // Servo PWM Output
    
    //Status Pins
    pinMode(WIFI_STATUS_PIN, OUTPUT); 
    
    //Input Pins
    pinMode(INPUT_0, INPUT);
    pinMode(INPUT_1, INPUT);
    pinMode(INPUT_2, INPUT);
    pinMode(INPUT_3, INPUT);

    //Output Pins
    ledcAttachPin(OUTPUT_0_PIN, OUTPUT_0_PWM);
    ledcAttachPin(OUTPUT_1_PIN, OUTPUT_1_PWM);
    ledcAttachPin(OUTPUT_2_PIN, OUTPUT_2_PWM);

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

void WriteToOutputs() {
    ledcWrite(OUTPUT_0_PWM, GPIO_PWM_DUTY_CYCLES[0]);
    ledcWrite(OUTPUT_1_PWM, GPIO_PWM_DUTY_CYCLES[1]);
    ledcWrite(OUTPUT_2_PWM, GPIO_PWM_DUTY_CYCLES[2]);
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

void WriteToClient(String message) {
    RemoteClient.print(message + ((message[-1] == '$') ? "" : "$"));
    Serial.println("[TCP][LOG]: >> " + message);
}

void InvalidCommand(String command) {
    Serial.println("[TCP][LOG]: Invalid Command Received: " + command);
    WriteToClient("Invalid Command");
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
            WriteToClient("ESP32 Server: Connection Accepted");
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

    // See command tree string for command structure.
    while (tcpCommandsToProcess.size() != 0) {
        String command = tcpCommandsToProcess.front();
        std::vector<String> commandComponents = Split(command, commandDelimiters);
        
        if (commandComponents.at(0) == "HELP?") {
            WriteToClient(TCP_COMMAND_TREE);
        }

        else if (commandComponents.at(0) == "*IDN?" || commandComponents.at(0) == "ID?") {
            WriteToClient(ID_STRING);    
        }

        else if (commandComponents.at(0) == "GPIO") {
            if (commandComponents.size() < 2) { InvalidCommand(command); } // No GPIO commands of this depth.
            
            else if (commandComponents.at(1) == "IN?") { //Input State Query
                WriteToClient((String)ReadInputs()); 
            }

            else if (commandComponents.at(1).startsWith("OUT(")) { //Output command
                
                int trailingCut = commandComponents.at(1).endsWith("?") ? 2 : 1;
                String outNumString = commandComponents.at(1).substring(4, commandComponents.at(1).length() - trailingCut);
                long outputNumber = outNumString.toInt(); // If invalid, will default to 0, will also ignore the ')' for queries
                if (outputNumber >= 0 && outputNumber <= 2) {
                    // Valid output selection
                    if (commandComponents.at(1).endsWith("?")) { //Output Query
                        float dutyCycle = ((float)GPIO_PWM_DUTY_CYCLES[outputNumber] / (float)GPIO_PWM_MAX_COUNT);
                        WriteToClient(String(dutyCycle, 4));   
                    }
                    else if (commandComponents.size() < 3) {  WriteToClient("No Output Value Detected"); }
                    else { // Output Write
                        int value = (int)(commandComponents.at(2).toFloat() * GPIO_PWM_MAX_COUNT); // Will default 0
                        if (value > GPIO_PWM_MAX_COUNT) {value = GPIO_PWM_MAX_COUNT;}
                        if (value < 0) {value = 0;}
                        GPIO_PWM_DUTY_CYCLES[outputNumber] = value;
                        WriteToOutputs();
                    }   
                }
                else { 
                    // Invalid output number.
                    WriteToClient("Invalid Output Number: " + outNumString);
                }
            }

            else {InvalidCommand(command);}
        }

        else {
            InvalidCommand(command);
        }
        
        // Done processing command
        tcpCommandsToProcess.erase(tcpCommandsToProcess.begin());
    }
}