# miniVDS
Are you tired of complicated VPN configurations? I just want a virtual network cable!

Here is a simplest distributed virtual switch running on LAN, only 200+ lines code. No configuration required, simply running miniVDS on different devices within the same LAN segment is equivalent to having separate network cards on each of these devices, and they are all connected on a virtual switch.

To run multiple virtual switches within the same LAN segment, you just need to specify different port numbers during runtime. Each port number corresponds to a virtual switch.

It supports Linux, OpenWrt, etc.

## Parameters

	-d, --daemon      Run in background.
	-h, --help        Show this document.
	-p, --port=PORT   Set the UDP port. Default port is 4444.
	                  Each port represent a virtual switch. Endpoints using different ports are not connected.
