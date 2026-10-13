# Compatibility tests

Run from an **x64 Native Tools Command Prompt for Visual Studio 2022** with the
`x64-windows-static` vcpkg dependencies installed (Detours 4.0.1 and MinHook 1.3.4):

```bat
tests\run.cmd C:\deps\x64-windows-static C:\temp\d2rhud-tests
```

The first argument is the directory containing `include` and `lib`; the second
is an explicit output directory outside the source tree. The runner builds with
`/MT`, leaves assertions enabled, stops at the first failure, and keeps generated
executables and object files in that output directory.

- `queue-test.cpp`: direct and adjusted queue pointers, ambiguous samples,
  mismatched vtables, unreadable memory, arithmetic overflow, and LastError
  preservation. Also exercises real MinHook/Detours chains in both orders and
  verifies callback counts, forwarding and detach behavior.

These tests do not load D2R or validate CrossOver/D3DMetal execution. Run the
queue harness on native Windows; its host-detection assertion expects that host.
