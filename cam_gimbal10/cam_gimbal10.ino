#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <AccelStepper.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

// Constants
#define DEFAULT_VISCA_IP "192.168.1.100"
#define DEFAULT_DEVICE_NAME "gimbalcontroller"
#define VISCA_PORT 52381
#define EEPROM_SIZE 512
#define MAX_PRESETS 9
#define MAX_SPEED 3000
#define DEFAULT_SPEED 500
#define ACCELERATION 1000
#define POSITION_LIMIT 10000
#define MAX_NAME_LENGTH 32
#define MAX_HOSTNAME_LENGTH 24

// Pin definitions
enum Pins {
  PIN_DIR_X = D1,
  PIN_STEP_X = D2,
  PIN_DIR_Y = D3,
  PIN_STEP_Y = D4,
  PIN_ENABLE = D5,
  PIN_REC = D6
};

// Control modes
enum ControlModes {
  WEB_ONLY,
  VISCA_ONLY,
  BOTH_MODE
};

// Camera control commands
enum CameraCommands {
  CMD_REC = 190,
  CMD_ZOOM_IN = 191,
  CMD_ZOOM_OUT = 192,
  CMD_FOCUS_IN = 193,
  CMD_FOCUS_OUT = 194,
  CMD_AUTO_FOCUS = 195
};

ESP8266WebServer server(80);
WiFiUDP udp;
char viscaIP[16] = DEFAULT_VISCA_IP;
char deviceName[MAX_NAME_LENGTH] = DEFAULT_DEVICE_NAME;
const uint16_t viscaPort = VISCA_PORT;

AccelStepper stepperX(AccelStepper::DRIVER, PIN_STEP_X, PIN_DIR_X);
AccelStepper stepperY(AccelStepper::DRIVER, PIN_STEP_Y, PIN_DIR_Y);

// Configuration variables
volatile int speed = DEFAULT_SPEED;
volatile bool motorsEnabled = true;
volatile bool invertX = false, invertY = false;
volatile int controlMode = BOTH_MODE;
volatile bool moveUp = false, moveDown = false, moveLeft = false, moveRight = false;
volatile bool storeMode = false;
volatile bool recallingPreset = false;

// EEPROM addresses
const int eepromViscaIPAddress = 0;
const int eepromControlModeAddress = 16;
const int eepromDeviceNameAddress = 32;
const int eepromPresetAddress = 64;

void validatePresets() {
    for (int i = 0; i < MAX_PRESETS; i++) {
        long xPos, yPos;
        EEPROM.get(eepromPresetAddress + i * sizeof(long) * 2, xPos);
        EEPROM.get(eepromPresetAddress + i * sizeof(long) * 2 + sizeof(long), yPos);

        if (xPos > POSITION_LIMIT || xPos < -POSITION_LIMIT || isnan(xPos)) {
            xPos = 0;
            EEPROM.put(eepromPresetAddress + i * sizeof(long) * 2, xPos);
        }
        if (yPos > POSITION_LIMIT || yPos < -POSITION_LIMIT || isnan(yPos)) {
            yPos = 0;
            EEPROM.put(eepromPresetAddress + i * sizeof(long) * 2 + sizeof(long), yPos);
        }
    }
    if (!EEPROM.commit()) {
        Serial.println("Failed to commit EEPROM changes");
    }
}

void moveMotors() {
    if (!motorsEnabled) {
        digitalWrite(PIN_ENABLE, HIGH);
        return;
    }
    
    digitalWrite(PIN_ENABLE, LOW);

    int xDirection = invertX ? -1 : 1;
    int yDirection = invertY ? -1 : 1;

    if (controlMode != VISCA_ONLY) {
        if (moveUp) stepperY.move(speed * yDirection);
        else if (moveDown) stepperY.move(-speed * yDirection);
        else stepperY.move(0);

        if (moveLeft) stepperX.move(-speed * xDirection);
        else if (moveRight) stepperX.move(speed * xDirection);
        else stepperX.move(0);
    }

    stepperX.run();
    stepperY.run();
}

// HTML page segments
const char htmlPage[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><title>)rawliteral";
const char htmlPageAfterTitle[] PROGMEM = R"rawliteral(</title><style>body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background-color:#121212;color:#e0e0e0;margin:0;padding:0;display:flex;justify-content:center;align-items:center;height:100vh}.container{background-color:#1e1e1e;padding:20px;border-radius:10px;box-shadow:0 4px 8px rgba(0,0,0,0.2);width:320px;text-align:center}h1{color:#fff;margin-bottom:20px}label{display:block;margin:10px 0 5px;color:#bbb}input[type="text"],input[type="range"],select{width:100%;padding:8px;margin-bottom:10px;border:1px solid #333;border-radius:5px;background-color:#333;color:#e0e0e0}input[type="range"]{-webkit-appearance:none;height:8px;background:#444;outline:none;opacity:0.7;transition:opacity 0.2s}input[type="range"]:hover{opacity:1}input[type="range"]::-webkit-slider-thumb{-webkit-appearance:none;width:16px;height:16px;background:#007bff;cursor:pointer;border-radius:50%}button{background-color:#007bff;color:white;border:none;padding:10px 20px;margin:5px;border-radius:5px;cursor:pointer;font-size:14px;transition:background-color 0.3s}button:hover{background-color:#0056b3}.checkbox-group{display:flex;justify-content:space-around;margin:10px 0}.checkbox-group label{display:flex;align-items:center;color:#bbb}.checkbox-group input[type="checkbox"]{margin-right:5px}.presets{margin-top:20px}.preset-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:5px;margin-top:10px}.preset-grid button{width:100%;height:40px;padding:0;margin:0;border-radius:5px;font-size:12px}.store-button{margin-top:10px;width:100%}</style></head><body><div class="container"><h1>)rawliteral";
const char htmlPageAfterH1[] PROGMEM = R"rawliteral(</h1><label>Device Name:</label><input type="text" id="deviceName" value=")rawliteral";
const char htmlPageAfterName[] PROGMEM = R"rawliteral(" onblur="setDeviceName(this.value)"><label>Control Mode:</label><select id="controlMode" onchange="setControlMode(this.value)"><option value="0">Web Only</option><option value="1">VISCA Only</option><option value="2" selected>Both</option></select><label>VISCA IP:</label><input type="text" id="viscaIP" value=")rawliteral";
const char htmlPageEnd[] PROGMEM = R"rawliteral(" onblur="setViscaIP(this.value)"><div><button onmousedown="move('up')" onmouseup="stop()">Up</button></div><div><button onmousedown="move('left')" onmouseup="stop()">Left</button><button onclick="home()">Home</button><button onmousedown="move('right')" onmouseup="stop()">Right</button></div><div><button onmousedown="move('down')" onmouseup="stop()">Down</button></div><label>Speed:</label><input type="range" min="20" max="3000" value="500" oninput="setSpeed(this.value)"><div class="checkbox-group"><label><input type="checkbox" id="enableMotors" onchange="toggleMotors(this.checked)" checked> Enable Motors</label><label><input type="checkbox" id="invertX" onchange="toggleInvertX(this.checked)"> Invert X</label><label><input type="checkbox" id="invertY" onchange="toggleInvertY(this.checked)"> Invert Y</label></div><div class="presets"><h3>Presets</h3><div class="preset-grid"><script>for(let i=0;i<9;i++){document.write(`<button onclick="recallPreset(${i})">P${i+1}</button>`)}</script></div><button class="store-button" onclick="storePreset()">Store</button></div><button onclick="rec()">Rec</button><button onclick="zoomin()">Zoom in</button><button onclick="zoomout()">Zoom out</button><button onclick="focusin()">Focus in</button><button onclick="focusout()">Focus out</button><button onclick="auto_focus()">Auto Focus</button><button onclick="rebootDevice()">Reboot</button></div><script>function move(dir){fetch(`/move?dir=${dir}`)}function stop(){fetch('/stop')}function rec(){fetch('/rec')}function zoomin(){fetch('/zoomin')}function zoomout(){fetch('/zoomout')}function focusin(){fetch('/focusin')}function focusout(){fetch('/focusout')}function auto_focus(){fetch('/auto_focus')}function setSpeed(val){fetch(`/speed?value=${val}`)}function toggleMotors(e){fetch(`/enable?state=${e?1:0}`)}function toggleInvertX(i){fetch(`/invertX?state=${i?1:0}`)}function toggleInvertY(i){fetch(`/invertY?state=${i?1:0}`)}function setViscaIP(ip){fetch(`/setViscaIP?ip=${ip}`)}function setDeviceName(name){fetch(`/setDeviceName?name=${encodeURIComponent(name)}`)}function setControlMode(m){fetch(`/setControlMode?mode=${m}`)}function storePreset(){fetch('/storePreset')}function recallPreset(i){fetch(`/recallPreset?index=${i}`)}function home(){fetch('/home')}function rebootDevice(){fetch('/reboot')}</script></body></html>)rawliteral";

void setup() {
    Serial.begin(115200);
    Serial.println("\nStarting Gimbal Controller...");
    
    // Initialize EEPROM
    EEPROM.begin(EEPROM_SIZE);
    
    // Load device name from EEPROM
    EEPROM.get(eepromDeviceNameAddress, deviceName);
    if (strlen(deviceName) == 0) {
        strncpy(deviceName, DEFAULT_DEVICE_NAME, sizeof(deviceName) - 1);
        deviceName[sizeof(deviceName) - 1] = '\0';
    }
    
    // Prepare hostname (lowercase, no spaces)
    String hostname = String(deviceName);
    hostname.replace(" ", "-");
    hostname.toLowerCase();
    if (hostname.length() > MAX_HOSTNAME_LENGTH) {
        hostname = hostname.substring(0, MAX_HOSTNAME_LENGTH);
    }
    
    // Initialize WiFiManager
    WiFiManager wifiManager;
    wifiManager.setDebugOutput(false);
    
    // Set AP name
    String apName = hostname.substring(0, min((int)hostname.length(), MAX_HOSTNAME_LENGTH-3)) + "-AP";
    
    // Attempt to connect to WiFi or start config portal
    if (!wifiManager.autoConnect(apName.c_str())) {
        Serial.println("Failed to connect and hit timeout");
        delay(3000);
        ESP.restart();
    }
    
    // WiFi connected, set hostname
    WiFi.hostname(hostname);
    Serial.printf("Connected to WiFi as %s\n", WiFi.localIP().toString().c_str());
    
    // Start mDNS responder
    if (MDNS.begin(hostname.c_str())) {
        Serial.printf("mDNS responder started. Access at: %s.local\n", hostname.c_str());
        MDNS.addService("http", "tcp", 80);
    } else {
        Serial.println("Error setting up mDNS responder!");
    }
    
    // Initialize hardware
    pinMode(PIN_ENABLE, OUTPUT);
    digitalWrite(PIN_ENABLE, LOW);
    
    stepperX.setMaxSpeed(MAX_SPEED);
    stepperY.setMaxSpeed(MAX_SPEED);
    stepperX.setAcceleration(ACCELERATION);
    stepperY.setAcceleration(ACCELERATION);

    // Load other settings
    EEPROM.get(eepromViscaIPAddress, viscaIP);
    if (strlen(viscaIP) == 0) strncpy(viscaIP, DEFAULT_VISCA_IP, sizeof(viscaIP) - 1);
    
    controlMode = EEPROM.read(eepromControlModeAddress);
    if (controlMode > BOTH_MODE || controlMode < WEB_ONLY) controlMode = BOTH_MODE;
    
    validatePresets();

    // Setup web server routes with fixed concatenation
    server.on("/", HTTP_GET, []() {
        String html = String(FPSTR(htmlPage)) + String(deviceName) + 
                     String(FPSTR(htmlPageAfterTitle)) + String(deviceName) + 
                     String(FPSTR(htmlPageAfterH1)) + String(deviceName) + 
                     String(FPSTR(htmlPageAfterName)) + String(viscaIP) + 
                     String(FPSTR(htmlPageEnd));
        server.send(200, "text/html", html);
    });

    server.on("/move", HTTP_GET, []() {
        if (server.hasArg("dir")) {
            String dir = server.arg("dir");
            moveUp = (dir == "up");
            moveDown = (dir == "down");
            moveLeft = (dir == "left");
            moveRight = (dir == "right");
        }
        server.send(200, "text/plain", "Moving");
    });

    server.on("/stop", HTTP_GET, []() {
        moveUp = moveDown = moveLeft = moveRight = false;
        server.send(200, "text/plain", "Stopped");
    });
    
    server.on("/speed", HTTP_GET, []() {
        if (server.hasArg("value")) {
            speed = constrain(server.arg("value").toInt(), 20, MAX_SPEED);
        }
        server.send(200, "text/plain", "Speed Updated");
    });

    server.on("/enable", HTTP_GET, []() {
        if (server.hasArg("state")) {
            motorsEnabled = server.arg("state").toInt() == 1;
            digitalWrite(PIN_ENABLE, motorsEnabled ? LOW : HIGH);
        }
        server.send(200, "text/plain", "Motors " + String(motorsEnabled ? "Enabled" : "Disabled"));
    });

    server.on("/invertX", HTTP_GET, []() {
        if (server.hasArg("state")) {
            invertX = server.arg("state").toInt() == 1;
        }
        server.send(200, "text/plain", "Invert X " + String(invertX ? "Enabled" : "Disabled"));
    });

    server.on("/invertY", HTTP_GET, []() {
        if (server.hasArg("state")) {
            invertY = server.arg("state").toInt() == 1;
        }
        server.send(200, "text/plain", "Invert Y " + String(invertY ? "Enabled" : "Disabled"));
    });

    server.on("/setViscaIP", HTTP_GET, []() {
        if (server.hasArg("ip")) {
            String ip = server.arg("ip");
            if (ip.length() > 0 && ip.length() < 16) {
                strncpy(viscaIP, ip.c_str(), sizeof(viscaIP) - 1);
                EEPROM.put(eepromViscaIPAddress, viscaIP);
                if (!EEPROM.commit()) {
                    server.send(500, "text/plain", "EEPROM Write Failed");
                    return;
                }
            }
        }
        server.send(200, "text/plain", "VISCA IP Updated");
    });

    server.on("/setDeviceName", HTTP_GET, []() {
        if (server.hasArg("name")) {
            String name = server.arg("name");
            if (name.length() > 0 && name.length() < MAX_NAME_LENGTH) {
                strncpy(deviceName, name.c_str(), sizeof(deviceName) - 1);
                deviceName[sizeof(deviceName) - 1] = '\0';
                EEPROM.put(eepromDeviceNameAddress, deviceName);
                if (!EEPROM.commit()) {
                    server.send(500, "text/plain", "EEPROM Write Failed");
                    return;
                }
                
                server.send(200, "text/plain", "Device Name Updated. Reboot to apply changes.");
            }
        }
    });

    server.on("/setControlMode", HTTP_GET, []() {
        if (server.hasArg("mode")) {
            controlMode = constrain(server.arg("mode").toInt(), WEB_ONLY, BOTH_MODE);
            EEPROM.write(eepromControlModeAddress, controlMode);
            if (!EEPROM.commit()) {
                server.send(500, "text/plain", "EEPROM Write Failed");
                return;
            }
        }
        server.send(200, "text/plain", "Control Mode Updated");
    });

    server.on("/storePreset", HTTP_GET, []() {
        storeMode = true;
        server.send(200, "text/plain", "Store Mode Enabled - Select preset to store");
    });

    server.on("/recallPreset", HTTP_GET, []() {
        if (server.hasArg("index")) {
            int index = constrain(server.arg("index").toInt(), 0, MAX_PRESETS-1);
            if (storeMode) {
                long xPos = stepperX.currentPosition();
                long yPos = stepperY.currentPosition();
                EEPROM.put(eepromPresetAddress + index * sizeof(long) * 2, xPos);
                EEPROM.put(eepromPresetAddress + index * sizeof(long) * 2 + sizeof(long), yPos);
                if (!EEPROM.commit()) {
                    server.send(500, "text/plain", "EEPROM Write Failed");
                    return;
                }
                storeMode = false;
                server.send(200, "text/plain", "Preset " + String(index + 1) + " Stored");
            } else {
                long xPos, yPos;
                EEPROM.get(eepromPresetAddress + index * sizeof(long) * 2, xPos);
                EEPROM.get(eepromPresetAddress + index * sizeof(long) * 2 + sizeof(long), yPos);
                stepperX.moveTo(xPos);
                stepperY.moveTo(yPos);
                recallingPreset = true;
                server.send(200, "text/plain", "Recalling Preset " + String(index + 1));
            }
        }
    });

    server.on("/home", HTTP_GET, []() {
        stepperX.moveTo(0);
        stepperY.moveTo(0);
        recallingPreset = true;
        server.send(200, "text/plain", "Moving to Home Position");
    });

    server.on("/reboot", HTTP_GET, []() {
        server.send(200, "text/plain", "Rebooting...");
        delay(100);
        ESP.restart();
    });

    // Camera control endpoints
    server.on("/rec", HTTP_GET, []() {
        Serial.write(CMD_REC);
        server.send(200, "text/plain", "Recording Toggled");
    });
    server.on("/zoomin", HTTP_GET, []() {
        Serial.write(CMD_ZOOM_IN);
        server.send(200, "text/plain", "Zooming In");
    });
    server.on("/zoomout", HTTP_GET, []() {
        Serial.write(CMD_ZOOM_OUT);
        server.send(200, "text/plain", "Zooming Out");
    });
    server.on("/focusin", HTTP_GET, []() {
        Serial.write(CMD_FOCUS_IN);
        server.send(200, "text/plain", "Focusing In");
    });
    server.on("/focusout", HTTP_GET, []() {
        Serial.write(CMD_FOCUS_OUT);
        server.send(200, "text/plain", "Focusing Out");
    });
    server.on("/auto_focus", HTTP_GET, []() {
        Serial.write(CMD_AUTO_FOCUS);
        server.send(200, "text/plain", "Auto Focus Activated");
    });

    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    MDNS.update();
    server.handleClient();
    
    // Handle incoming VISCA packets
    int packetSize = udp.parsePacket();
    if (packetSize) {
        byte packet[50];
        udp.read(packet, packetSize);
        // Process VISCA packet
    }

    if (recallingPreset) {
        stepperX.run();
        stepperY.run();
        if (stepperX.distanceToGo() == 0 && stepperY.distanceToGo() == 0) {
            recallingPreset = false;
        }
    } else {
        moveMotors();
    }
}