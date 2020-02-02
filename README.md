# V2
This tool allows you to connect to HM-10 BLE module directly from Raspberry 
PI using it's built-in Bluetooth module so you don't need the second HM-10. 
The tool exposes this connection over a TCP socket.

Using this tool you can, for example, easily control the state of the pins 
of HM-10 directly from Pi terminal (note that your HM-10 should be in mode 
1 or 2, see datasheet for more details):

```bash
echo -n "AT+PIO21" | nc -vN localhost 9000  #Set HM-10 PIO2 to high
echo -n "AT+PIO20" | nc -vN localhost 9000  #Set HM-10 PIO2 to low
```

Or you can use TCP sockets in your program and exchange the binary data 
with your HM-10 module.

## Hardware
This utility was developed on Raspberry Pi 3B+. But I suppose it should work 
on any Pi that has Bluetooth >=4.0 module. I used the original HM-10 BLE 
module. You probably know about the clone of HM-10, that uses different AT 
command set. I suppose it works the similar way but I have not tested it yet.

## Build the tool
You should build the tool before using. First of all install required libraries:
```bash
sudo apt install libglib2.0-dev
```

Clone this repository to some path and build it:
```bash
cd /tmp
https://github.com/mironovdm/rpi2hm10
make
```

If compilation is finished successfully then copy the compiled binary 
`rpi2hm10` to any other directory you want, for example to `/usr/local/bin`.

## How to use
At first you need to know MAC address of your HM-10 module. You can use `bluetoothctl` tool on your Pi for that. Power up your HM-10, start `bluetoothctl` and enable scaning. During the scan you may see several devices, one of them should be called "HMSoft" - it is default name for HM-10:
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

And you are ready to start the tool. Look at command below. You have to change in the same manner the part `dev_6A_75_1E_6E_E8_99` in the command with your MAC that you have found out earlier :
```bash
sudo pi2hm10 localhost 9000 /org/bluez/hci0/dev_6A_75_1E_6E_E8_99 /org/bluez/hci0/dev_6A_75_1E_6E_E8_99/service0010/char0011
```