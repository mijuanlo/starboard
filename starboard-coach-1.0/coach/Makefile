obj-m := coach.o
KDIR  := /lib/modules/$(KERNELRELEASE)/build
PWD   := $(shell pwd)
KBUILD_CFLAGS += -fno-stack-protector -Wstack-protector -Wall

KERNELRELEASE := $(shell uname -r)

TARGETS := $(KERNELRELEASE)/coach.ko

default:
	mkdir -p $(KERNELRELEASE)
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	mv -f coach.ko $(KERNELRELEASE)

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

insmod:
	modprobe videodev
	modprobe usbcore
	modprobe videobuf-core
	modprobe videobuf-vmalloc
	insmod coach.ko

modprobe:
	modprobe videodev
	modprobe usbcore
	modprobe videobuf-core
	modprobe videobuf-vmalloc

rmmod:
	-rmmod coach

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
