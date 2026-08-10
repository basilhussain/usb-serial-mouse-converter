@echo off
start /min openocd -f interface/wlink.cfg -f target/wch-riscv.cfg
start riscv-none-elf-gdb -ex "target extended-remote localhost:3333" %1
