tx2mon
=====

The tx2mon package contains:
 - a kernel module (tx2mon_kmod) which provides access to system
   specific data and config areas using sysfs
 - an application (tx2mon) which uses this sysfs interface to monitor
   sensors.

Adding the kernel module
------------------------
You can use the DKMS infrastructure to add the tx2mon_kmod module:

root# dkms add modules/dkms.conf

root# dkms install -m tx2mon_kmod -v 0.1

If you do not use DKMS, you could call 'make' and build it using the
standard way to build external modules.

Loading the kernel module
-------------------------
If the module is built with dkms, use modprobe to load the module:

root# modprobe tx2mon_kmod

In case you use the external module build, use insmod(8) to load
the module

Building the tx2mon application
-------------------------------
You will need termcap libraries for this, install 'libncurses5-dev'
or similar package for your distribution for this.

See tx2mon/README for information on how to use the application.
