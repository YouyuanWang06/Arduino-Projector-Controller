# Arduino-Projector-Controller
An end-to-end project that remotely controls a projector and a motorized screen, syncronized with a remote player, using multiple relays, arduino (MCU), driver module and CAN bus communication module.

## Overview
This system centralizes control over multiple audio-visual devices into a single embedded controller. It accepts both manual physical button inputs and automated network commands over a CAN bus to synchronize motorized screen deployment, projector commands, and audio/media playback.

### Key Features
* **Multi-Protocol Control:** Combines serial communication (**RS232**), network triggers (**CAN bus**), and dry-contact switching (**3-channel relays**).
* **Trio Input Modes:** Supports both remote CAN bus triggers, Serial monitor input and local physical pushbuttons for manual override and testing.
* **Synchronized A/V Sequencing:** Coordinates motorized screen movement, projector power/source states, and remote media player triggers.

---

## Hardware Architecture
* **Microcontroller:** Arduino Nano 
* **Communication Modules:** 
  * CAN Bus Transceiver Module (MCP2515)
  * RS232-to-TTL Serial Converter (MAX3232) 
* **Switching & Actuation:** 3-Channel Relay Module
* **Peripherals & I/O:**
  * Projector (controlled via driver module commands)
  * Motorized Projection Screen (controlled via relay dry contacts)
  * Remote Media Player & Audio Output (controlled via RS232 serial signal)
  * Physical Control Pushbuttons (For testing purpose)
  * Dedicated DC Power Supply

---

## System Wiring & Schematics
<img width="1280" height="1707" alt="c5af1b1072600fe99ec7712e7e566341" src="https://github.com/user-attachments/assets/4c490031-4c9c-470d-9b75-d73d4370cec5" />

## System Wiring & Power Architecture

```mermaid
flowchart LR
    subgraph PSU["Power Distribution (24V DC Main)"]
        DC24["24V DC Power Supply"]
        BUCK1["24V → 5V Buck Converter #1"]
        BUCK2["24V → 5V Buck Converter #2"]
        DC24 -->|"24V DC"| DRIVER
        DC24 --> BUCK1
        DC24 --> BUCK2
        BUCK1 -->|"5V DC (VCC/GND)"| NANO
        BUCK2 -->|"5V DC"| PLAYER["Remote Media Player"]
    end

    subgraph NANO["Arduino Nano (on Expansion Board)"]
        D0D1["D0 / D1 (Hardware Serial)"]
        D4["D4"]
        D5["D5"]
        D6D7["D6 (RX) / D7 (TX) + 3.3V / GND"]
        D8["D8"]
        D9["D9"]
        D10_13["D10 (CS), D11 (MOSI), D12 (MISO), D13 (SCK)"]
        A2_A4["A2, A3, A4 (INPUT_PULLUP)"]
        A5["A5"]
    end

    subgraph IO["Inputs & Communication"]
        USB["USB / Debug Serial Monitor"]
        BTNS["Test Pushbuttons\n(A2: Proj Up | A3: Proj Down | A4: Play)"]
        RS232["RS232-to-TTL Converter (3.3V)"]
        CAN["MCP2515 CAN Bus Module"]
    end

    subgraph ACT["Relays, Drivers & Actuators"]
        REL_UP["Relay 1: Screen Up (NO)"]
        REL_DN["Relay 2: Screen Down (NO)"]
        REL_PWR["Relay 3: Remote Power"]
        DRIVER["24V Motor Driver Module\n(IN4: Down | IN3: Up)"]
        SCREEN["Motorized Projection Screen"]
        ACTUATOR["24V Projector Lift Actuator"]
    end

    D0D1 <--> USB
    BTNS --> A2_A4
    D6D7 <-->|"SoftwareSerial (9600)"| RS232
    RS232 <-->|"RS232"| PLAYER
    D10_13 <-->|"SPI (125 kbps)"| CAN

    D4 --> REL_UP
    D5 --> REL_DN
    REL_UP -->|"Dry Contact (NO)"| SCREEN
    REL_DN -->|"Dry Contact (NO)"| SCREEN

    D8 -->|"IN4 (Down)"| DRIVER
    D9 -->|"IN3 (Up)"| DRIVER
    DRIVER -->|"24V Output"| ACTUATOR

    A5 --> REL_PWR
    REL_PWR -->|"Power Trigger"| PLAYER
```

### Pinout & Power Summary

| Arduino Nano Pin | Connected Module / Circuit | Signal / Function |
| :--- | :--- | :--- |
| `D0` / `D1` | Hardware Serial (USB) | Serial monitor debugging and status logs (`9600 baud`) |
| `D2` / `D3` | *Unconnected* | Reserved from initial bench testing |
| `D4` | Relay Channel 1 (`Screen-Relay-Up`) | Normally Open (NO) dry-contact pulse to raise screen |
| `D5` | Relay Channel 2 (`Screen-Relay-Low`) | Normally Open (NO) dry-contact pulse to lower screen |
| `D6` / `D7` | RS232-to-TTL Converter (`RX` / `TX`) | `SoftwareSerial` commands to remote media player (`3.3V` powered) |
| `D8` | Motor Driver (`IN4`) | Drives 24V linear actuator to lower projector |
| `D9` | Motor Driver (`IN3`) | Drives 24V linear actuator to raise projector |
| `D10` – `D13` | CAN Bus Module (`CS`, `MOSI`, `MISO`, `SCK`) | SPI communication with master controller (`125 kbps`) |
| `A2` / `A3` / `A4` | Physical Test Pushbuttons | Manual overrides: Projector Up (`A2`), Down (`A3`), Play (`A4`) |
| `A5` | Relay Channel 3 (`Power Relay`) | Switches remote power relay for media/projector system |
| `Expansion VCC/GND` | 24V→5V Buck Converter #1 | Dedicated 5V logic supply (isolated from media player's 5V buck) |


### Pinout Summary

| Arduino Nano Pin | Connected Component | Signal / Purpose |
| :--- | :--- | :--- |
| `D2` / `D3` | RS232 Transceiver (TX/RX) | SoftwareSerial commands to projector |
| `D4` – `D6` | 3-Channel Relay Module | Screen Up / Down / Media trigger |
| `D7` – `D8` | Physical Buttons | Manual local control inputs |
| `D10` – `D13` | CAN Bus Module (SPI) | CS, MOSI, MISO, SCK |

*(Update the pin numbers above to match your exact schematic.)*

---

## Firmware & Control Logic
The firmware is written in **C++ (Arduino framework)** and structured around a state-based trigger loop:
1. **Input Polling:** Monitors incoming CAN bus frames and debounced physical button states.
2. **Command Translation:** Maps trigger events to specific RS232 hex/ASCII strings for the projector and digital HIGH/LOW pulses for the relay channels.
3. **Timing & Sequencing:** Manages actuation delays to ensure the motorized screen and audio-visual playback stay synchronized.

---

## Testing & Troubleshooting
To ensure reliable operation in a live environment, the system went through iterative hardware and firmware validation:
* **Serial Verification:** Used a USB-to-RS232 adapter and serial monitor to verify baud rate, parity, and exact command strings before connecting the projector.
* **Electrical Isolation & Continuity:** Checked relay switching states, power rail stability, and ground references with a multimeter to prevent signal noise or voltage drops during motor actuation.
* **Bench Testing:** Simulated CAN bus trigger messages and tested manual pushbutton overrides to verify debounce timing and fault recovery.

---

## Repository Structure
```text
├── src/                  # Arduino C++ source code (.ino / .cpp)
├── diagrams/             # Electrical schematics and wiring diagrams
└── README.md             # Project documentation
```

