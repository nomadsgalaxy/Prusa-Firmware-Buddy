# CORE One L electronics architecture

**CORE One L** printer features new `ac_controller` daughterboard (ACC)
responsible for managing AC power to the new silicone heatbed.
ACC communicates with the rest of the system via **Cyphal over CAN**.
Access to the CAN bus is provided by `xBuddyExtension` daughterboard (XBE) which
was already introduced in **CORE One** printer to provide additional
peripherals for the `xBuddy` motherboard.
Motherboard communicates with XBE via **MODBUS**.

```
 .--------.  MODBUS  .-----------------.   Cyphal/CAN  .---------------.
 | xBuddy | <------> | xBuddyExtension | <-----+-----> | ac_controller |
 '--------'          '-----------------'       |       '---------------'
                                               |
                                               `-------> rest of the CAN bus
 \_____________________________________/ \__________________________________/
    preexisting CORE One electronics        new electronics for CORE One L
```


# Bootstrap
Successful communication between all these boards requires careful
orchestration and robust error handling.


## Bootstrap (xBuddy)
1. Copy firmware for XBE and ACC from USB drive to XFLASH on xBuddy.[^1]
2. Run puppy[^2] bootstrap protocol for XBE.
   1. Reset XBE via reset pin, the XBE should run the bootloader.
   2. Check if XBE is running the bootloader by checking bootloader protocol
      version and hardware info.
   3. Verify firmware fingerprint using salted hash computation.
   4. Flash new firmware if fingerprint mismatch detected.
   5. Start application firmware.

3. Wait for ACC to become ready. As soon as XBE firmware starts, it exposes
   ACC as a virtual modbus device. The bootstrap of the ACC is managed
   by the XBE in the background.


## Bootstrap (XBE)
After xBuddy finishes bootstrapping the XBE, the XBE takes over the bootstrap
process for the ACC through the CAN bus:

1. **Discovery**: XBE implements Cyphal PnP protocol, acting as an allocator
   for all the other nodes on the CAN bus. It also listens to node heartbeats
   and requests node information.

2. **Flashing**: Cyphal nodes are generally running modified
   Kocherga bootloader. XBE acts as a firmware transfer proxy between xBuddy
   and a particular Cyphal node.
   1. When Cyphal node requests firmware chunks, XBE stores the request
      to be handled by xBuddy later.
   2. xBuddy reads XBE status and detects that there is a firmware chunk request.
   3. xBuddy reads firmware chunk from XFLASH and sends it back to XBE.
   4. XBE forwards firmware chunk to Cyphal node.

3. **Verification**: To prevent tampering, Cyphal node is asked to compute
   a salted, cryptographically secure hash of its firmware. The premise is that the storage
   on the Cyphal node is not large enough to fit both original and unoriginal firmware.
   XBE asks xBuddy to compute the hash in a similar fashion and compares the two hashes.
   This mechanism is also used to facilitate firmware updates.


# Regular operation
After successful bootstrap, the system enters regular operation mode where the
puppy task continuously monitors and controls all daughterboards through periodic
MODBUS communication. ACC broadcasts regular status updates over CAN, which XBE
receives and makes available to the xBuddy. xBuddy makes configuration requests
for ACC, which are forwarded through XBE.


# Overview of MODBUS addresses on CORE One L
| Address | Description                                    |
|---------|------------------------------------------------|
| 0x11    | XBE bootloader                                 |
| 0x21    | XBE application                                |
| 0x22    | ACC virtual device provided by XBE application |
| 0xdc    | MMU virtual device provided by XBE application |


# Bus specifics
MODBUS runs over RS485 line at 230400 bauds, half duplex. Communication
is always initiated by xBuddy, XBE can only respond.

Cyphal runs over CAN-FD, limited to 125 kbps, due to electric characteristics
of the bus and unknown topology of the network. Communication can be initiated by
any node.

For instructions on how to access and debug the CAN bus, see [pub6-debugging.md](pub6-debugging.md).


[^1]: This step is necessary and can't be skipped. Files need to be
      available during each reboot of the xBuddy in order to compute
      salted hash and verify daughterboard firmware integrity.


[^2]: In general, there can be multiple physical devices on the MODBUS.
      Those are called **puppies**. Some of them have more complicated
      bootstrap protocol involving assigning addresses. Since CORE One L
      only has one puppy XBE with fixed address, this documentation
      describes only the simple protocol without dynamic addresses.
