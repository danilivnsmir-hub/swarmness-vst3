# SWARMNESS benchmark firmware for Daisy Seed (libDaisy, MIT).
#   git clone --recurse-submodules https://github.com/electro-smith/libDaisy ../libDaisy
#   make -C ../libDaisy && make && make program-dfu     (hold BOOT, tap RESET first)
TARGET = swarmness_bench

LIBDAISY_DIR ?= ../libDaisy
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# ~150 KB of code: runs from QSPI flash via the Daisy bootloader (make program-boot once, then make program-dfu)
APP_TYPE ?= BOOT_QSPI

CPP_SOURCES = main.cpp
C_INCLUDES += -I../../../Source -I../../bench
CPP_STANDARD = -std=gnu++17
C_DEFS += -DSWARM_NO_JUCE=1

include $(SYSTEM_FILES_DIR)/Makefile
