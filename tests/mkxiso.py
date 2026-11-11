"""mkxiso.py XBE BASE OUT: a small XDVDFS image for tests/android_storage_test.c.

The game partition holds default.xbe (the file given) and vc_53450030/test.bin,
starting at byte BASE of OUT: 0 for an xiso, 0x18300000 for a full (Redump)
disc dump, whose first part (the video partition) is left as zeros."""
import struct, sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"


def entry(left, right, start, size, attr, name):
    e = struct.pack("<HHIIBB", left, right, start, size, attr, len(name)) + name
    return e + b"\xff" * (-len(e) % 4)


def main():
    xbe = open(sys.argv[1], "rb").read()
    base = int(sys.argv[2], 0)
    test = b"vc file " * 1000
    root_sec, vc_sec, xbe_sec = 33, 34, 36
    test_sec = xbe_sec + (len(xbe) + SECTOR - 1) // SECTOR
    e0 = entry(0, 0, xbe_sec, len(xbe), 0x20, b"default.xbe")
    e1 = entry(0, 0, vc_sec, SECTOR, 0x10, b"vc_53450030")
    e0 = entry(0, len(e0) // 4, xbe_sec, len(xbe), 0x20, b"default.xbe")   # "vc..." sorts after: right child
    root = (e0 + e1).ljust(SECTOR, b"\xff")
    vc = entry(0, 0, test_sec, len(test), 0x20, b"test.bin").ljust(SECTOR, b"\xff")
    vd = MAGIC + struct.pack("<II", root_sec, SECTOR) + b"\0" * 8
    vd = vd.ljust(0x7EC, b"\0") + MAGIC
    vd = vd.ljust(SECTOR, b"\0")
    with open(sys.argv[3], "wb") as f:
        def at(sec, data):
            f.seek(base + sec * SECTOR)
            f.write(data)
        at(32, vd)
        at(root_sec, root)
        at(vc_sec, vc)
        at(xbe_sec, xbe)
        at(test_sec, test)
        f.truncate(base + (test_sec + 8) * SECTOR)
    print("wrote", sys.argv[3], "base", hex(base))


main()
