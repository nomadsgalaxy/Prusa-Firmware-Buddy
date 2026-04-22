# PUB6 debugging

PUB6 (Prusa Universal Bus) is a bus used to connect additional devices to the printer.
The physical layer is provided by CAN-FD running at 125 kbps.
A low baud rate is chosen because network topology is not known in advance.
On top of CAN-FD, we use Cyphal to provide higher-level framing and addressing.

For system architecture and communication protocols, see [electronics.md](electronics.md).

## Hardware setup
PUB6 can be accessed using one of two female Molex 502585-0470 connectors
marked `PUB CAN` on the right side of the `xBuddyExtension` board.
Both connectors are wired in parallel and can be used simultaneously.
The corresponding male connector is Molex 502578-0400, which you may either crimp yourself
or use a `Modular bed cable` (ID 8837) replacement from the Prusa e-shop.
Connector pinout is as follows:
 * 1 = CAN-
 * 2 = CAN+
 * 3 = GND
 * 4 = not connected

We recommend using the `candleLight FD` USB to CAN-FD adapter,
which is a low-cost **Open Hardware** and **Open Software** product with excellent Linux support.
You can find all the information on the [product page](https://linux-automation.com/en/products/candlelight-fd.html).

## Software setup
If you use candleLight and Linux, you first need to set up the `can0` device using a command such as
```
ip link set dev can0 up type can bitrate 125000 dbitrate 125000 fd on
```

You can verify if there are any messages on the bus using
```
candump can0
```

If messages are present, you can use a utility script to decode them.
```
utils/cyphal_dump.py
```
