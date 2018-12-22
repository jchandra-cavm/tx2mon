txmon
=====

Adding kernel module
--------------------
You can use dkms to add the module:

root# dkms add modules/dkms.conf

root# dkms install -m tx2mon_kmod -v 0.1

Or you could call 'make' and build it using the standard
build process.

Building application
--------------------
You will need termcap libraries for this, install 'libncurses5-dev'
or similar package for your distribution for this.

See tx2mon/README for information on how to use the application.
