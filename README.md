# genesis_wifi_flash_cart_8mbit
Genesis / Megadrive 8 MBit (1MByte) flash cart re-writeable over WiFi

# ! WORK IN PROGRESS !

![prototype](https://github.com/ole00/genesis_wifi_flash_cart_8mbit/raw/master/img/cart_r3.jpg "prototype cart")

Here is yet another Genesis / Megadrive flash cart, this time around it is possible
to upload the game/program on the fly over WiFi while the cart is plugged in the console.
It is an open source / open hardware project: all schematics, BOMS, gerbers, design files, firmware
will be available here once the project evolves from the WIP stage. Some sources are available even now.


**Build difficulty**: Medium

The cart uses an existing WiFi development board ([Wemos Lolin S2-mini](https://www.wemos.cc/en/latest/s2/s2_mini.html))
which simplifies the construction of the wireless part of the cart. Then the rest of the components
are SMT parts with SOP pin pitch (1.25mm), with the exception of the LDO which has 1mm pitch.
Such project should be possible to build at home by enthusiasts with medium skills
and a decent soldering iron (TS100 or similar with heat regulation).

The cart also contains an optional GAL IC which adds some
extra features. If the GAL IC is not populated, the cart should still work, but with basic features only.
The WiFi board needs to be programmed, but it should be straightforward as the board does not require
extra programmer, it can be reprogrammed over USB (see the USB-C connector on the right side of the 
WiFi board) from Arduino IDE. The GAL chip also needs to be programmed - that has to be done only once and requires
either generic EEPROM programmer (like TL866ii Plus or similar that can program GAL16V8 or ATF16V8B ICs)
or you could build [Afterburner GAL chip programmer](https://github.com/ole00/afterburner) with Arduino UNO
on a bread board. If that seems complicated you can add the GAL IC to the cart later, whenever
it is convenient for you.

If that sounds too complicated for you, check the basic megadrive cart [here](https://github.com/ole00/genesis_flash_cart_8mbit).
That one uses through hole parts only, but the flash chips need to be programmed out-of-the-cart by an external
eeprom programmer.

**Costs**: Low, around 40-60$, most of the costs are actually shipment costs of the PCBs and components.
To reduce costs team up with your friends to produce few of the carts and split the costs.

**Basic operation:**

When the cart flash chips are programmed then after console power up the game plays nearly instantly
(about 0.3 seconds of delay after power is turned on). The WiFi board creates an AP which can be accessed
either via phone, tablet or computer. The WiFi board also hosts a web page on an IP address 192.168.4.1 which
allows to select a ROM file and program the flash chips. After the programming is done the console has to be
power cycled. Once that is done the new game starts.

**Extra features (require the programmed GAL chip):**

- console print outs (logs from you genesis game) over WiFi via UDP netcat.
- extra 64kb of Slow RAM hosted on the WiFi MCU
- game saves for homebrew games (total space of about 600 kbytes) hosted on the WiFi MCU
  for score boards, game progress etc.
- possibly also data download/upload over Wifi - will not be supported right after the first cart FW release
  because of the higher software complexity. 
  
**How it works electrically:**

There are 2 flash chips that hold the data of the Genesis game. One chip keeps the odd bytes, the other one keeps 
the even bytes. Both of the chips are connected to the address and data bus of the console. These chips use 5 Volt 
logic and require 5V supply which the console provides. The WiFi board and its MCU (micro controller) is also connected
to the address and data bus of the console, but not directly. There are bunch of so called transceivers that provide a 'bridge' between
the console bus and the WiFi MCU. The role of the transceivers is:
- voltage translation between 5V and 3.3V logic required by the WiFi MCU
- they can completely detach and attach the WiFi MCU from the buses. That is required to avoid data collisions on the buses.
- they so called 'latch' the incoming or outgoing data on the bus and keep them in an internal register. This helps to process
  the data with relaxed timing (set data sooner, read data later) in a way that suits the WiFi MCU.
  
When the user starts the flash IC programming the main CPU of the console is held in reset, which (in most of the cases) is enough
to detach the console from the buses. The WiFi MCU can then reprogram the flash chips. In some cases there are some background
HW tasks that access the bus even when the console's main CPU is held in reset. That causes collisions on the bus and the cart programming
over WiFi fails. When that happens the user is notified about it and is requested to press the Reset button on the console.
After the Reset button is pressed the flash IC programming can be re-tried. When the flash IC programming is done the main
CPU of the console is released from the reset, the WiFi MCU gets detached from the buses (turns off the transceivers)
and the console can start the new game. Most of the games require power cycle to play them correctly right after programming.


**FAQ:**

Q: What is the purpose of the USB port on the WiFi MCU?

A: This is only for writing the firmware to the WiFi MCU. This should be done while the cart is OUT of the console!!!
   Never EVER connect the USB cable between the cart and the PC while the cart is plugged in the console! This could damage
   the cart, your console and your PC. Do not even think of doing that! If you need to change the WiFi MCU firmware
   ALWAYS take the cart out of the console and then connect it to your PC with a USB cable - the PC will provide enough power to
   the cart.

--------

Q: do I need to upload the game every time I power on the console ?

A: no, you upload it once and it stays programmed. You can power cycle the console as many times as you want and the game stays there.
   You can also unplug he cart and plug it to a friend's console and the game will still play.

--------

Q: The cart provides only 1MB of storage, can it be upgraded to 2MBytes?

A: not easily with this dual flash IC configuration. The board would have to be redesigned to accommodate a different type of a flash chip.

--------

Q: The extra RAM the cart provides, why do you call it Slow RAM?

A: because it can be accessed only over an 8 bit parallel bus with sequential addressing. That means the Genesis program can access
   it byte by byte and the address is auto increasing from the initially set address. The regular Genesis RAM can access 16 bits of data
   directly on any valid RAM address - this is much faster.

--------

Q: I want to build this cart. Can you provide gerber files?

A: PCB Rev3 is now verified, you can find gerbers in the 'gerbers' directory. Upload the zip file to the PCB manfacturing online service
(I used JLCPCB service, but other services should work OK with that zip file as well, if not let me know). The design has 4 coppe layers, and their
order (the website may ask you to specify this)  is 1) GTL, 2) GL2, 3) GL3, 4) GBL. Thickness of the board is 1.6mm.

--------

Q: Isn't 4 layer board too expensive to produce? Could you simplify the desgin to use 2 layer PCB?

A: Not really. These days (2022) you can get the 4 layer PCBs for less than $15 (without postage) and you get several PCBs for that price. I doubt
the PCB can be simplified to 2 layer desgin, but maybe some PCB guru could do that. Try it yourself - the PCB design is free and open
and can be edited in [Geda PCB CAD tool](http://pcb.geda-project.org) wich is also free and open source project.

--------

Q: How far is the WIP stage - what is still missing?

A: I have verified the PCB Rev3 is correct and functional, it and can be built without bodge wires and PCB hacks (see the prototype image).
I have implemented reading and writing from the slow memory (see mcw-memtest in the examples directory).
I 'd like to add few functions for storing and reading application data to/from the WiFi MCU flash storage. 
Then write a Genesis test programs to exercise the new code code. 
The stored data will need some simple meanagement Web page accessible over HTTP server running on the cart.
Apart from that the basic functionality (flashing the cart over WiFi, reading and writing the slow memory) is finished and should work now.

--------

Q: I would like to fit the PCB  cart into a plastic shell - which plastic shell can I use?

A: You can use a 3d printed case that I used in the [previous genesis cart design](https://github.com/ole00/genesis_flash_cart_8mbit).
The .stl files can be found in the 'case' directory. There is also some extra information at the bottom of the readme.md page.
   
