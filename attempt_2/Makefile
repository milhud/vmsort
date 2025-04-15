# Kernel module
obj-m += vmsort.o

# Default target - compile both kernel module and user program
all: module driver

# Kernel module compilation
module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

# User space driver compilation
driver: driver.c
	gcc -o driver driver.c -Wall -Werror

# Clean up
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f driver

# Script to set up the device
setup: module
	-sudo rmmod vmsort
	sudo insmod vmsort.ko
	MAJ=$$(grep vmsort /proc/devices | awk '{print $$1}'); \
	if [ -n "$$MAJ" ]; then \
		sudo rm -f /dev/vmsort; \
		sudo mknod /dev/vmsort c $$MAJ 0; \
		sudo chmod 666 /dev/vmsort; \
		echo "Created /dev/vmsort with major number $$MAJ"; \
	else \
		echo "Failed to find major number for vmsort"; \
	fi

# Run the driver with appropriate permissions
run: driver
	sudo ./driver
