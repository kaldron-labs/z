factor out pid file creation/reading from `zi/src/ZiDaemon.cc` into `ZiPIDFile.{hh,cc}`:
- scrutinize implementation for race condititions and repair
- `ZiDaemon` should use the new `ZiPIDFile` implementation
- all pid files should be prefixed with `/var/run/`, or whatever the appropriate prefix is for the O.S. and distro:
  - this should use similar (almost identical) logic to `Ztls.hh:2029-`
- update all `ZiDaemon` dependents to NOT pass an absolute path for `pidFile`
