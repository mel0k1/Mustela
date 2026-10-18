import struct
import sys


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: pe_exports.py <dll>")
    path = sys.argv[1]
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise SystemExit("not a PE file")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
        raise SystemExit("bad PE signature")
    coff = e_lfanew + 4
    nsections, _, _, _, opt_size, _ = struct.unpack_from("<HIIIHH", data, coff + 2)
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    dd_off = opt + (112 if magic == 0x20B else 96)
    exp_rva = struct.unpack_from("<I", data, dd_off)[0]
    if exp_rva == 0:
        raise SystemExit("no export directory")
    sec_off = opt + opt_size
    sections = []
    for i in range(nsections):
        s = sec_off + i * 40
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", data, s + 8)
        sections.append((vaddr, max(vsize, rsize), roff))

    def rva2off(rva):
        for vaddr, size, roff in sections:
            if vaddr <= rva < vaddr + size:
                return roff + (rva - vaddr)
        raise SystemExit("rva not mapped: 0x%X" % rva)

    eo = rva2off(exp_rva)
    _, _, _, _, _, base, nfuncs, nnames, _, aon, aono = struct.unpack_from(
        "<IIHHIIIIIII", data, eo)
    name_off = rva2off(aon)
    ord_off = rva2off(aono)
    out = []
    for i in range(nnames):
        name_rva = struct.unpack_from("<I", data, name_off + i * 4)[0]
        start = rva2off(name_rva)
        end = data.index(b"\x00", start)
        out.append(data[start:end].decode("utf-8", "replace"))
    sys.stdout.write("\n".join(out))
    if out:
        sys.stdout.write("\n")


if __name__ == "__main__":
    main()
