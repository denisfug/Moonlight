#!/usr/bin/env python3
"""Checks that create-fself kept the whole initialized data of the linked ELF in eboot.bin.

The create-fself of the OpenOrbis v0.5.2 package cut the writable segment after ".data"
(dropping .data.__libc, .data.__stderr_FILE, ...): the app then closed at launch with
CE-34878-0 (builds 01.24-01.27). Usage: check_eboot.py <linked elf> <eboot.bin>"""
import struct
import sys


def sections(d):
    shoff = struct.unpack_from('<Q', d, 40)[0]
    shes, shn, shstr = struct.unpack_from('<HHH', d, 58)
    hs = [struct.unpack_from('<IIQQQQIIQQ', d, shoff + i * shes) for i in range(shn)]
    stroff = hs[shstr][4]
    out = []
    for name, typ, flags, addr, off, size, *_ in hs:
        end = d.index(b'\0', stroff + name)
        out.append((d[stroff + name:end].decode(), typ, flags, addr, size))
    return out


def main():
    elf = open(sys.argv[1], 'rb').read()
    ebo = open(sys.argv[2], 'rb').read()
    secs = sections(elf)
    start = [a for n, t, f, a, s in secs if n == '.data.sce_process_param']
    if not start:
        sys.exit('check_eboot: .data.sce_process_param not found in the ELF')
    # end of the last allocated section that has file contents (SHT_NOBITS = 8, SHF_ALLOC = 2)
    end = max(a + s for n, t, f, a, s in secs if (f & 2) and t != 8 and a >= start[0])
    want = end - start[0]
    nseg = struct.unpack_from('<H', ebo, 0x18)[0]
    e = 0x20 + nseg * 0x20
    if ebo[e:e + 4] != b'\x7fELF':
        sys.exit('check_eboot: eboot.bin has no ELF header where expected')
    phoff = struct.unpack_from('<Q', ebo, e + 32)[0]
    phes, phn = struct.unpack_from('<HH', ebo, e + 54)
    got = tls = None
    for i in range(phn):
        pt, pf, po, pv, pp, pfs, pms, pa = struct.unpack_from('<IIQQQQQQ', ebo, e + phoff + i * phes)
        if pt == 1 and pv == start[0]:
            got = pfs
        if pt == 7:
            tls = pa
    if got != want:
        sys.exit('check_eboot: FAIL: initialized data is 0x%x bytes in the ELF but 0x%x in eboot.bin '
                 '(create-fself cut it; this build would close at launch). Use the v0.5.4 toolchain.'
                 % (want, got or 0))
    print('check_eboot: ok (initialized data 0x%x bytes kept, TLS alignment 0x%x)' % (want, tls or 0))


if __name__ == '__main__':
    main()
