This repo is an ongoing project to reverse engineer the handshake between the TopDon TC001 USB Thermal Camera to enable use in private applications.

Decoding the handshake was done on a Windows 11 PC through USB-C and analyzing packets with Wireshark.
The bootup sequence and handshake with the device has been recreated in C for performance reasons and has successfully been utilized in a homemade Android application.
