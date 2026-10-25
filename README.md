# Peoject env logger

Set `SD_CARD_DUMP_MODE` to `1` in `main/config.h` to build the SD-card dump
mode. In this mode, the application initializes only the SD card, reads
`/sdcard/environment.csv`, and logs its contents one line at a time. Set the
macro back to `0` to build the normal environment-logging mode.
