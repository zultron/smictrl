# SMI enable register manipulation tool

This utility by [Jan Kiszka][jan-git] turns on/off SMI register flags,
useful for [fixing SMI-related latency issues][lcnc-smi] in real-time environments
running on Intel chipsets.

This repo includes Marty Vona's [changes][marty-changes], and John
Morris's Debian packaging with init scripts for running at system
boot.

[jan-git]: http://git.kiszka.org/?p=smictrl.git
[lcnc-smi]: http://wiki.linuxcnc.org/cgi-bin/wiki.pl?FixingSMIIssues
[marty-changes]: http://wiki.linuxcnc.org/uploads/smictrl.c
