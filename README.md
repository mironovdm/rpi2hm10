This tool allows you to connect to HM-10 BLE module directly from Raspberry 
PI using it's built-in Bluetooth module so you don't need the second HM-10 
connected to UART. The tool exposes this connection over a TCP socket.

Using this tool you can, for example, easily control the state of the pins 
of HM-10 directly from Pi terminal (note that your HM-10 should be in mode 
1 or 2, see datasheet for more details):

```bash
echo -n "AT+PIO21" | nc -vN localhost 9000  #Set HM-10 PIO2 to high
echo -n "AT+PIO20" | nc -vN localhost 9000  #Set HM-10 PIO2 to low
```

Or you can use TCP sockets in your program and exchange the binary data 
with your HM-10 module.

## Important note
This tool works with HM-10 V551 and lower version firmware and doesn't work with V710 
firmware because of a problem with activation of notifications on HM-10, which responds 
with error. I have not tested versions between V551 and V710.

It seems that it is possible to make V710 work updating some data in bluetoothd cache 
but it's not a reliable way.

## Hardware
This utility was developed on Raspberry Pi 3B+. But I suppose it should work 
on any Linux powered device with Bluez and BLE support.

You can use original HM-10 modules made by Jinan Huamao as well as HM-10 clones
wich use another AT-command set, but the same principal of exposing serial 
connection. Clone just use another BLE service and characteristic identifiers. 
See more about this in the section __How to use__.

## Building the tool
You should build the tool before using. First of all ensure you have required libraries installed:
```bash
sudo apt install libglib2.0-dev
```

Clone this repository to some path and build it:
```bash
cd /tmp
git clone https://github.com/mironovdm/rpi2hm10.git
cd rpi2hm10
make
```

If compilation is finished successfully then copy the compiled binary 
`rpi2hm10` to any other directory you want, for example to `/usr/local/bin`.

## How to use
At first you need to know MAC address of your HM-10 module. You can use `bluetoothctl` tool on your Pi for that purpose. Power up your HM-10, start `bluetoothctl` and enable scaning. During the scan you may see several devices, one of them should be called "HMSoft" - it is a default name for HM-10:
```
pi@raspberrypi:~ $ sudo bluetoothctl
Agent registered
[bluetooth]# scan on
[NEW] Device 6A:75:1E:6E:E8:99 HMSoft
```

Now disable the scan:
```
[bluetooth]# scan off
Discovery stopped
```

And you are ready to start the tool. Look at the command below. You have to change in the same manner the part `dev_6A_75_1E_6E_E8_99` in the command below with your MAC address that you have found out earlier. Note that you need root privileges to access Bluetooth:
```bash
sudo ./rpi2hm10 \
--host localhost --port 5000 \
--dev /org/bluez/hci0/dev_6A_75_1E_6E_E8_99 \
--char /org/bluez/hci0/dev_6A_75_1E_6E_E8_99/service0010/char0011
```

#### HM-10 clone
For HM-10 clone you should use another service and characteristic: replace
`service0010/char0011` in the --char command argument with `service0023/char0024`.
