"""GDB-only growth counters for zfbiobench; run separately from timing.

libtool exec gdb -q -batch -x zfb/interop/trace-growth.py --args \
  zfb/interop/zfbiobench 3 65536 1

Counts Z allocator requests made inside Bench::run,
including run setup/teardown. Fixed-pool requests and variable-size requests
are separate; backing malloc calls inside the pools are not counted again.
The fixed-pool breakpoint covers the benchmark's 64-byte-aligned I/O pools.
Counts growth-path memcpy/memmove calls and bytes at their actual call sites
in the retained x86-64 release binary, using the SysV size argument register.
Checks that both growth functions contain all three expected copy sites
(in-place tail move, relocated head and relocated tail); fails if the compiler
changes this shape. Payload copies, scalar stores and scratch writes are not
counted here. Copy calls are not growth events: one relocation may copy both
head and tail, while empty-buffer initialization copies neither.
Capacity extensions exclude initialization from zero logical capacity.
Debugger timings are not latency measurements.
"""

import gdb
import json
import re

counts = {}


def current_row():
    caller = gdb.newest_frame()
    while caller:
        name = caller.name() or ""
        if "Bench::run<Bench::" in name:
            codec = "canonical" if "Canonical" in name else "native"
            mode = "fresh" if bool(caller.read_var("fresh")) else "reuse"
            key = codec + "/" + mode
            row = counts.setdefault(key, dict(
                pool_requests=0, variable_requests=0,
                pool_empty_requests=0,
                variable_bytes=0, variable_sizes={},
                capacity_extensions=0, empty_extensions=0,
                growth_copy_calls=0, growth_copy_bytes=0))
            return key, row
        caller = caller.older()
    return None, None


class Pool(gdb.Breakpoint):
    def stop(self):
        # Variable allocations use the same cache; count them only below.
        frame = gdb.newest_frame()
        while frame:
            if "ZmVHeap_" in (frame.name() or ""):
                return False
            frame = frame.older()
        key, row = current_row()
        if row is not None:
            row["pool_requests"] += 1
            cache = gdb.newest_frame().read_var("this").dereference()
            if not int(cache["m_head"]["m_val"]):
                row["pool_empty_requests"] += 1
        return False


class Variable(gdb.Breakpoint):
    def stop(self):
        key, row = current_row()
        if row is not None:
            row["variable_requests"] += 1
            size = int(gdb.newest_frame().read_var("size"))
            row["variable_bytes"] += size
            sizes = row["variable_sizes"]
            sizes[size] = sizes.get(size, 0) + 1
        return False


class Copy(gdb.Breakpoint):
    def stop(self):
        key, row = current_row()
        if row is not None:
            row["growth_copy_calls"] += 1
            row["growth_copy_bytes"] += int(gdb.parse_and_eval("$rdx"))
        return False


class Grow(gdb.Breakpoint):
    def __init__(self, address, native):
        super().__init__("*" + address, internal=True)
        self.native = native

    def stop(self):
        key, row = current_row()
        if row is not None:
            if self.native:
                arena = gdb.parse_and_eval("$rdi").cast(gdb.lookup_type(
                    "Zfb::Arena<Zfb::IOStorage<8u> >").pointer())
                old = int(arena.dereference()["m_capacity"])
                empty = not (int(arena.dereference()["m_head"]) or
                             int(arena.dereference()["m_size"]))
            else:
                old = int(gdb.parse_and_eval("$rdx"))
                empty = not (int(gdb.parse_and_eval("$r8")) or
                             int(gdb.parse_and_eval("$r9")))
            if old:
                row["capacity_extensions"] += 1
                if empty:
                    row["empty_extensions"] += 1
        return False


def copies():
    if gdb.newest_frame().architecture().name() != "i386:x86-64":
        raise RuntimeError("copy trace requires the retained x86-64 build")
    for name in (
        "Zfb::Arena<Zfb::IOStorage<8u> >::grow_(unsigned int)",
        "Bench::IOBuilder::reallocate_downward(unsigned char*, unsigned long, "
        "unsigned long, unsigned long, unsigned long)",
    ):
        code = gdb.execute("disassemble '" + name + "'", to_string=True)
        sites = []
        for line in code.splitlines():
            if re.search(r"\bcall\b.*<(?:memcpy|memmove)@plt>", line):
                sites.append(re.search(r"0x[0-9a-f]+", line).group())
        if len(sites) != 3:
            raise RuntimeError("unexpected growth copy sites in " + name)
        Grow(re.search(r"0x[0-9a-f]+", code).group(), name.startswith("Zfb"))
        for site in sites:
            Copy("*" + site, internal=True)


gdb.execute("set pagination off")
gdb.execute("set breakpoint pending on")
# The allocation cache entry survives release inlining of operator new.
Pool("void* ZmHeapCache::alloc<64u>()", internal=True)
# First executable line of the variable allocator, before size is rounded.
Variable("zm/src/ZmVHeap.hh:82", internal=True)
exits = []
gdb.events.exited.connect(lambda event: exits.append(
    getattr(event, "exit_code", None)))
try:
    gdb.execute("start")
    copies()
    gdb.execute("continue")
    if exits != [0]:
        raise RuntimeError("benchmark did not exit successfully")
    if set(counts) != {codec + "/" + mode
                      for codec in ("canonical", "native")
                      for mode in ("reuse", "fresh")}:
        raise RuntimeError("incomplete allocation trace")
    gdb.write("allocation counters: " + json.dumps(counts, sort_keys=True) + "\n")
except Exception as error:
    gdb.write("allocation trace failed: " + str(error) + "\n", gdb.STDERR)
    gdb.execute("quit 1")
