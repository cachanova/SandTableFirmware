# Local TMC2209 library

This directory vendors `janelia-arduino/TMC2209` version 9.4.2 under its
3-clause BSD license.

The local copy changes initialization order for machine safety. It writes
`IHOLD=0` and `IRUN=0` before disabling analog current scaling, then writes
`CHOPCONF.TOFF=0` as the first chopper configuration. Upstream 9.4.2 writes
`IRUN=31` and `TOFF=3` before reducing current and disabling the bridge, which
can create a high-current pulse when `ENN` is low during controller startup.

The local copy also consumes the single-wire UART echo after each register
write. Upstream leaves those bytes queued, which can fill the ESP32 receive
path during a multi-register configuration and corrupt the next checked
transaction.
