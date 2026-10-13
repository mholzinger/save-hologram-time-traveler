#!/usr/bin/env python3
"""Build a 3DS level-3 RomFS image from a folder, and attach it to a .3dsx.

    .venv/bin/python -I tools/romfs.py DIR OUT.romfs
    .venv/bin/python -I tools/romfs.py --3dsx BASE.3dsx DIR OUT.3dsx

The image is what libctru's romfsInit reads (3dbrew "RomFS", level 3):
  header   10 x u32: header size 0x28, then offset and size of the directory hash table, directory
           metadata, file hash table, file metadata, and the offset of the file data
  hash     u32 bucket heads (first entry's metadata offset, or 0xFFFFFFFF)
  dir      {u32 parent, next sibling, first child dir, first file, next in bucket, name bytes, UTF-16 name}
  file     {u32 parent, next sibling, u64 data offset, u64 size, u32 next in bucket, name bytes, UTF-16 name}
  data     right after the file metadata; each file 4-byte aligned
Names are padded to 4 bytes. In a .3dsx the image is appended after the SMDH and its offset goes in the
extended header at 0x28 (smdh offset, smdh size, romfs offset at 0x20).
"""

import os
import struct
import sys

NONE = 0xFFFFFFFF


def bucket_count(n):
    """Hash table size for n entries, as the SDK's tools choose it."""
    if n < 3:
        return 3
    if n < 19:
        return n | 1
    while any(n % p == 0 for p in (2, 3, 5, 7, 11, 13, 17)):
        n += 1
    return n


def name_hash(parent, name):
    h = parent ^ 123456789
    for unit in struct.unpack(f"<{len(name.encode('utf-16-le')) // 2}H", name.encode("utf-16-le")):
        h = (((h >> 5) | (h << 27)) & 0xFFFFFFFF) ^ unit
    return h


def align(n, a):
    return (n + a - 1) // a * a


class Dir:
    def __init__(self, name, parent, off):
        self.name, self.parent, self.off = name, parent, off
        self.first_dir = self.first_file = None
        self.sibling = None


class File:
    def __init__(self, name, parent, off, path, data_off, size):
        self.name, self.parent, self.off, self.path = name, parent, off, path
        self.data_off, self.size = data_off, size
        self.sibling = None


def utf16(name):
    return name.encode("utf-16-le")


def build(root_dir, order=sorted):
    """The level-3 image of root_dir as bytes, laid out as devkitPro's 3dsxtool does (src/romfs.cpp):
    a depth-first walk; each entry is added to the front of its parent's lists, so listing a folder gives
    the reverse of the walk order; file data 4-byte aligned. `order` sorts each folder's entries: sorted
    (the default) gives the same image on every OS; os.listdir order reproduces 3dsxtool on this machine.
    Hidden entries (".name") are skipped, as 3dsxtool does."""
    dirs, files = [], []
    sizes = {"dir": 0, "file": 0, "data": 0}

    def add_dir(name, parent):
        d = Dir(name, parent, sizes["dir"])
        dirs.append(d)
        sizes["dir"] = align(sizes["dir"] + 0x18 + len(utf16(name)), 4)
        return d

    def scan(d, path):
        for e in order(os.listdir(path)):
            if e.startswith("."):
                continue
            p = os.path.join(path, e)
            if os.path.isdir(p):
                c = add_dir(e, d)
                c.sibling, d.first_dir = d.first_dir, c
                scan(c, p)
            else:
                size = os.path.getsize(p)
                f = File(e, d, sizes["file"], p, sizes["data"], size)
                files.append(f)
                sizes["file"] = align(sizes["file"] + 0x20 + len(utf16(e)), 4)
                sizes["data"] = align(sizes["data"] + size, 4)
                f.sibling, d.first_file = d.first_file, f

    root = add_dir("", None)
    scan(root, root_dir)

    def off(x):
        return x.off if x else NONE

    dir_buckets, file_buckets = [NONE] * bucket_count(len(dirs)), [NONE] * bucket_count(len(files))
    dir_next, file_next = {}, {}
    for d in dirs:
        b = name_hash((d.parent or d).off, d.name) % len(dir_buckets)
        dir_next[d.off], dir_buckets[b] = dir_buckets[b], d.off
    for f in files:
        b = name_hash(f.parent.off, f.name) % len(file_buckets)
        file_next[f.off], file_buckets[b] = file_buckets[b], f.off

    dir_meta = bytearray()
    for d in dirs:
        n = utf16(d.name)
        dir_meta += struct.pack(
            "<6I", (d.parent or d).off, off(d.sibling), off(d.first_dir), off(d.first_file), dir_next[d.off], len(n)
        )
        dir_meta += n.ljust(align(len(n), 4), b"\0")
    file_meta = bytearray()
    for f in files:
        n = utf16(f.name)
        file_meta += struct.pack("<2I2Q2I", f.parent.off, off(f.sibling), f.data_off, f.size, file_next[f.off], len(n))
        file_meta += n.ljust(align(len(n), 4), b"\0")

    dir_hash = struct.pack(f"<{len(dir_buckets)}I", *dir_buckets)
    file_hash = struct.pack(f"<{len(file_buckets)}I", *file_buckets)
    o_dh = 0x28
    o_dm = o_dh + len(dir_hash)
    o_fh = o_dm + len(dir_meta)
    o_fm = o_fh + len(file_hash)
    o_data = o_fm + len(file_meta)
    header = struct.pack(
        "<10I", 0x28, o_dh, len(dir_hash), o_dm, len(dir_meta), o_fh, len(file_hash), o_fm, len(file_meta), o_data
    )
    out = bytearray(header + dir_hash + dir_meta + file_hash + file_meta)
    for f in files:
        with open(f.path, "rb") as fh:
            out += fh.read()
        out += b"\0" * (align(len(out), 4) - len(out))
    return bytes(out)


def attach(base_3dsx, romfs):
    """base_3dsx (no RomFS yet) with the RomFS image appended and its offset in the extended header."""
    magic, header_size = struct.unpack_from("<4sH", base_3dsx, 0)
    assert magic == b"3DSX" and header_size >= 0x2C, "not a .3dsx with an extended header"
    assert struct.unpack_from("<I", base_3dsx, 0x28)[0] == 0, "the .3dsx already has a RomFS"
    out = bytearray(base_3dsx)
    struct.pack_into("<I", out, 0x28, len(out))
    return bytes(out) + romfs


def main():
    a = sys.argv[1:]
    if a[:1] == ["--3dsx"]:
        base, folder, out = a[1:4]
        with open(base, "rb") as f:
            data = attach(f.read(), build(folder))
    else:
        folder, out = a[:2]
        data = build(folder)
    with open(out, "wb") as f:
        f.write(data)
    print(f"{out}: {len(data)} bytes")


if __name__ == "__main__":
    main()
