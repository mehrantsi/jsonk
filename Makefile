# JSONK - High-Performance JSON Library for Linux Kernel
# Copyright (C) 2025 Mehran Toosi
# Licensed under GPL-2.0

# Module name
obj-m := jsonk.o
obj-m += basic_usage.o
obj-m += performance_test.o
obj-m += atomic_test.o

# Source files
jsonk-objs := src/jsonk.o
basic_usage-objs := examples/basic_usage.o
performance_test-objs := tests/performance_test.o
atomic_test-objs := tests/atomic_test.o

# Kernel build directory
KDIR ?= /lib/modules/$(shell uname -r)/build

# Default target
all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Clean target
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f Module.symvers modules.order

# Install target
install: modules
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a

# Uninstall target
uninstall:
	rm -f /lib/modules/$(shell uname -r)/extra/jsonk.ko
	rm -f /lib/modules/$(shell uname -r)/extra/basic_usage.ko
	rm -f /lib/modules/$(shell uname -r)/extra/performance_test.ko
	rm -f /lib/modules/$(shell uname -r)/extra/atomic_test.ko
	depmod -a

# Load module
load:
	insmod jsonk.ko

# Unload module
unload:
	rmmod jsonk

# Load basic usage module
load-basic: load
	insmod basic_usage.ko

# Unload basic usage module
unload-basic:
	rmmod basic_usage

# Load performance test module
load-perf: load
	insmod performance_test.ko

# Unload performance test module
unload-perf:
	rmmod performance_test

# Load atomic test module
load-atomic: load
	insmod atomic_test.ko

# Unload atomic test module
unload-atomic:
	rmmod atomic_test

# Test basic usage
test-basic: load-basic
	dmesg | tail -50

# Test performance
test-perf: load-perf
	dmesg | tail -100

# Test atomic operations
test-atomic: load-atomic
	dmesg | tail -30

# Help target
help:
	@echo "Available targets:"
	@echo "  all       - Build all modules"
	@echo "  clean     - Clean build files"
	@echo "  install   - Install the modules"
	@echo "  uninstall - Uninstall the modules"
	@echo "  load      - Load the jsonk module"
	@echo "  unload    - Unload the jsonk module"
	@echo "  load-basic - Load basic usage module"
	@echo "  unload-basic - Unload basic usage module"
	@echo "  load-perf - Load performance test module"
	@echo "  unload-perf - Unload performance test module"
	@echo "  load-atomic - Load atomic test module"
	@echo "  unload-atomic - Unload atomic test module"
	@echo "  test-basic - Test basic usage"
	@echo "  test-perf - Test performance"
	@echo "  test-atomic - Test atomic operations"
	@echo "  help      - Show this help"

.PHONY: all modules clean install uninstall load unload load-basic unload-basic load-perf unload-perf load-atomic unload-atomic test-basic test-perf test-atomic help 