#!/bin/sh

openocd --file utils/cproject/A3ides-Debug-OpenOCD.cfg --command "tpiu config internal /dev/stdout uart off 168000000"
