# CORE One L runtime hardware detection

CORE One and CORE One L have separate firmware builds.
To prevent users from flashing incompatible firmware,
the bootloader implements a hardware compatibility check.

## Hardware differences

The key difference is the heated bed implementation:

- **CORE One**: Heated bed thermistor connected directly to xBuddy MCU
- **CORE One L**: Heated bed managed by `ac_controller` over PUB6 bus (Cyphal/CAN), no thermistor connection to xBuddy

See [electronics.md](electronics.md) for full CORE One L architecture details.

## Detection mechanism

We detect the hardware variant by checking for the heated bed thermistor via ADC reading on xBuddy:

- **Thermistor present** → CORE One
- **Thermistor absent** → CORE One L

Detection runs in the bootloader before starting application firmware.
The mechanism is simple enough to fit in the flash-constrained bootloader.
On mismatch, the bootloader prevents incompatible firmware from being flashed.
The same mechanism is also used when the firmware starts.
