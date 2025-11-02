#!/bin/bash
# Flash script for ESP32-S3 with all required partitions

cd "$(dirname "$0")/build" || exit 1

esptool.py --chip esp32s3 \
  -p /dev/cu.usbmodem1401 \
  -b 921600 \
  --before=default_reset \
  --after=hard_reset \
  write_flash \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size 16MB \
  0x0 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0x20000 xiaozhi.bin
