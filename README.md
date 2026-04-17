# Automatic Watering System

An automatic watering system based on **ESP32**, with a local web control panel for both **mobile** and **desktop** browsers.

## Features

- Control the water pump from a web page
- Accessible from both phone and computer on the same local network
- Real-time device status display
- Watering timer
- Watering count statistics
- Simulated soil moisture display
- ESP32 + MOS module + water pump solution

## Requirements

### Hardware

- ESP32 development board
- MOS driver module (12V / 24V, depending on your pump)
- DC water pump
- DC power adapter matching the pump voltage
- Jumper wires
- Tubing / water container

### Software

- Arduino IDE
- ESP32 board package for Arduino

## Hardware Setup

> Make sure the voltage of the **pump**, **MOS module**, and **power adapter** matches.

Typical connection:

- ESP32 `GPIO` -> MOS module `HIGH/PWM` (or control input)
- ESP32 `GND` -> MOS module `GND`
- Power adapter `+` -> MOS module `VIN+`
- Power adapter `-` -> MOS module `VIN-`
- Pump `+` -> MOS module `OUT+`
- Pump `-` -> MOS module `OUT`

## Mobile Control

<p align="center">
  <img src="https://github.com/MustangYM/Automatic-Watering/blob/main/phone.jpg" width="250px"/>
</p>

## Computer Control

<p align="center">
  <img src="https://github.com/MustangYM/Automatic-Watering/blob/main/mac.png" width="750px"/>
</p>

## Demo

<p align="center">
  <img src="https://github.com/MustangYM/Automatic-Watering/blob/main/demo.jpg" width="750px"/>
</p>

## How to Use

1. Open the project in Arduino IDE
2. Select the correct ESP32 board and serial port
3. Configure your Wi-Fi name and password in the code
4. Upload the firmware to the ESP32
5. Open the Serial Monitor and get the local IP address
6. Visit the IP address in your phone or computer browser
7. Control the pump from the web interface

## Notes

- The phone and computer must be connected to the **same local network** as the ESP32
- Do **not** connect the pump directly to the ESP32
- The pump should be driven through the MOS module
- If you use a different voltage pump, update the hardware accordingly

## License

MIT
