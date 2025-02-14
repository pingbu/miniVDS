# miniVDS

Are you tired of complicated VPN configurations? Will you afford the price of VLAN switches? I just want a virtual network cable!

Here is a simplest distributed virtual switch running on LAN, only 200+ lines code. No configuration required, simply running miniVDS on different devices within the same LAN segment is equivalent to having separate network cards on each of these devices, and they are all connected on a virtual switch.

To run multiple virtual switches within the same LAN segment, you just need to specify different port numbers during runtime. Each port number corresponds to a virtual switch.

It supports Linux, OpenWrt, etc. Compilation requires to link the *pthread* library.

[Latest Binary Download](https://github.com/pingbu/miniVDS/releases/tag/1.0), includes binary files for OpenWrt (mipsel, x86_64) and Ubuntu (x64).

## Parameters

	-d, --daemon      Run in background.
	-h, --help        Show this document.
    -i, --itf=ITF     Force the TAP interface name.
	-p, --port=PORT   Set the UDP port. Default port is 4444.
	                  Each port represent a virtual switch. Endpoints using different ports are not connected.

## OpenWrt

### Auto Startup

Install dependent modules:

	opkg update
	opkg install libstdcpp6 kmod-tun

Download *miniVDS* into directory */root*.

	Main Menu → System → Startup → Local Startup

Insert the following line before "exit 0":

	/root/miniVDS -d

Save and reboot.

	Main Menu → Network → Interfaces → Devices

Now you can see a new virtual "Network device" in the list, which is commonly named "tap0". You can use it like a real network device.

### Bind A Physical Switch Port 

If you want to bind a physical switch port into the virtual switch, you can create a new bridge.

	Main Menu → Network → Interfaces → Devices → Add device configuration…

 	Device type: "Bridge device"
  	Device name: such as "br-minivds"
  	Bridge ports: check your virtual device and selected switch ports, such as "tap0" and "lan2"

	Save

Save and apply.

 	Main Menu → Network → Interfaces → Interfaces → Add new interface...

	Name: such as "miniVDS"
	Protocol: Unmanaged
	Device: bridge created just now, such as "br-minivds"

	Create interface

Save and apply.

## Ubuntu

### Auto Startup

Download *miniVDS* into directory */usr/bin*. Insert the following line in */etc/rc.local* before "exit 0":

	/usr/bin/miniVDS -d

Save and reboot.

	ip addr

Now you can see a new virtual "Network device" in the list, which is commonly named "tap0". You can use it like a real network device.

### Allocate An IP Address

Edit */etc/netplan/00-installer-config.yaml*

	network:
	  ethernets:
	    ens32:
	      ...
	    tap0:
	      dhcp4: false
	      addresses:
	        - 192.168.88.8/24
	version: 2

Save.

	sudo netplan apply
