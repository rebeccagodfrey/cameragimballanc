# ESP8266 LANC-Controlled Camera Gimbal

![3D Printed Gimbal with Camera](docs/images![Camera Gimbal](https://github.com/user-attachments/assets/678f2cbf-d083-4ea2-b25e-12a536f71439)


**Modernizing legacy cameras** - A WiFi-controlled 3D printed gimbal system that adds remote pan/tilt control and LANC camera functions to older professional video cameras.

## Project Concept

This system bridges the gap between older professional cameras (like Sony DSR-500, Canon XL series, etc.) and modern remote operation needs by providing:

- 🖥️ **Web-based control interface** accessible from any device
- 🤖 **3D printed pan-tilt mechanism** for smooth camera movement thats supports a handy cam or DSLR
- 📡 **WiFi remote control** of camera functions via LANC protocol
- 📱 **Mobile-friendly interface** with tactile controls

### Key Features

| Camera Control | Gimbal Control | Connectivity |
|---------------|----------------|--------------|
| ▶️ Start/Stop Recording | ↔️ Pan Control | 📶 WiFi Web Interface |
| 🔍 Zoom In/Out | ↕️ Tilt Control | 🌐 mDNS (hostname.local) |
| 🎚️ Focus Control | 💾 Position Presets | 📱 Works on Mobile |
| ⚙️ Auto Focus | 🏠 Home Position | |

## Hardware Implementation

### Gimbal Mechanics
- Based on [this 3D printable gimbal design](https://www.myminifactory.com/object/3d-print-camera-gimbal-95406)
- Modified for heavier professional cameras
- NEMA 17 stepper motors with microstepping drivers

### LANC Control Circuit
- Implemented using [LANC protocol specifications](https://www.boehmel.de/lanc.htm)
- Hardware design inspired by [Arduino LANC Controller](https://controlyourcamera.blogspot.com/2011/02/arduino-powered-lanc-remote.html)


## Technical Details

### Components
- **Controller**: ESP8266 (WEMOS D1)
- **Motors**: 2x NEMA 17 steppers
- **Drivers**: TMC2208 stepper drivers. You can use a4988 drivers but they make the motors quite noisy. TMC drivers are almost silent. https://www.aliexpress.com/item/1005008483274991.html?spm=a2g0o.order_list.order_list_main.11.13861802D5nhUf 
- **LANC Interface**: Arduino nano
- **Power**: 12V 3A power supply

### Software Features
- Responsive web interface
- mDNS support (`gimbalcontroller.local`)
- EEPROM storage for presets
- Motor acceleration control
- VISCA protocol support (yet untested)

## Getting Started

### 1. Hardware Assembly
1. Print the [gimbal mechanism](https://www.myminifactory.com/object/3d-print-camera-gimbal-95406)
2. Assemble with stepper motors
3. Build the stepper control circuit
4. Build LANC interface circuit
5. Wire all components
