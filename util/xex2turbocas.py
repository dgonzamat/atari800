#!/usr/bin/env python3
"""Build a TurboSoft turbo cassette (.CAS) from an Atari executable (.XEX).

TurboSoft was one of the Spanish "turbo" tape systems of the mid eighties.
Its loader is not the Atari SIO one: it polls SKSTAT directly, runs the
data at 790 baud instead of 600, and protects itself with a bit stream that
is painted on tape as raw two-tone pulses.  Nothing in the emulator can
produce such a tape, so the only way to get an arbitrary program onto one
was to run the original TURBO800 recorder under emulation and let it write
the tape in real time.  This script does it directly instead.

It works by keeping a real tape as a model.  Everything up to the first data
block - the boot record, the loader body, the synchronisation tone and the
two encrypted $98 blocks - is identical in every TurboSoft tape examined
(River Raid, Zorro and Spy Hunter), so it is reused verbatim, and so is the
protection tail after the last block.  Only the data blocks are rebuilt.

The payload the loader expects is

    1536-byte prologue  +  the .XEX, unchanged

The prologue is the same 12 blocks in all three tapes; it is the routine
that walks $D822-$DDFB with EOR #$29 and decrypts the loader body that the
$98 blocks deposited there.  Since it does not vary with the program, it can
be lifted from the model tape as it stands.

A data block is 134 bytes:

    55 55 | control | 128 data bytes | count_lo | count_hi | checksum

The control byte is $FC in an ordinary block and $FA in the last block of a
segment - the prologue is a segment of its own, so its block 11 already
carries $FA, and the program ends with another one.  A separate $FE block
whose 128 data bytes are zero closes the tape.  The count is sequential from
zero and the checksum is the usual SIO sum with end-around carry over the
first 133 bytes.  Inter-block gaps are copied from the model and the last
one is repeated when the program needs more blocks than the model had.

Rebuilding a model tape from its own .XEX reproduces it byte for byte.

    usage: xex2turbocas.py <model.cas> <program.xex> <output.cas>
"""
import struct
import sys
import os

PROLOGUE = 1536                 # 12 blocks exactly
DATA = 128                      # payload bytes per block
BLOCK = 134


def read_cas(path):
    """Split a .CAS into its description and a list of [type, aux, data]."""
    f = open(path, 'rb')
    if f.read(4) != b'FUJI':
        sys.exit("%s is not a CAS file" % path)
    length, aux = struct.unpack('<HH', f.read(4))
    description = f.read(length)
    chunks = []
    while True:
        header = f.read(8)
        if len(header) < 8:
            break
        length, aux = struct.unpack('<HH', header[4:8])
        chunks.append([header[0:4], aux, f.read(length)])
    return description, chunks


def checksum(block):
    """SIO_ChkSum(): add up every byte but the last, with end-around carry."""
    total = 0
    for byte in block[:-1]:
        total += byte
        if total >= 256:
            total = (total & 0xFF) + 1
    return total


def main(model_path, xex_path, out_path):
    description, chunks = read_cas(model_path)
    model = [c for c in chunks if c[0] == b'data' and len(c[2]) == BLOCK]
    if not model:
        sys.exit("%s has no %d-byte data blocks: not a TurboSoft tape"
                 % (os.path.basename(model_path), BLOCK))

    prologue = b''.join(bytes(c[2][3:3 + DATA]) for c in model)[:PROLOGUE]
    if len(prologue) < PROLOGUE:
        sys.exit("the model tape is too short to hold a prologue")

    xex = open(xex_path, 'rb').read()
    if xex[:2] != b'\xff\xff':
        sys.exit("%s does not start with $FFFF: not an Atari executable"
                 % os.path.basename(xex_path))

    payload = prologue + xex
    count = (len(payload) + DATA - 1) // DATA
    payload += bytes(count * DATA - len(payload))

    gaps = [c[1] for c in model]
    steady = gaps[-2] if len(gaps) > 2 else 98
    while len(gaps) < count + 1:
        gaps.append(steady)

    end_of_prologue = PROLOGUE // DATA - 1
    blocks = []
    for i in range(count + 1):          # the data blocks plus the closing one
        if i == count:
            control, chunk = 0xFE, bytes(DATA)
        else:
            control = 0xFA if i in (end_of_prologue, count - 1) else 0xFC
            chunk = payload[i * DATA:(i + 1) * DATA]
        block = bytearray(b'\x55\x55') + bytes([control]) + chunk
        block += bytes([i & 0xFF, (i >> 8) & 0xFF, 0])
        block[BLOCK - 1] = checksum(block)
        blocks.append([b'data', gaps[i], bytes(block)])

    out, placed = [], False
    for c in chunks:
        if c[0] == b'data' and len(c[2]) == BLOCK:
            if not placed:              # the new blocks go where the old ones were
                out.extend(blocks)
                placed = True
            continue
        out.append(c)

    image = bytearray()

    def add(kind, aux, data):
        image.extend(kind)
        image.extend(struct.pack('<HH', len(data), aux))
        image.extend(data)

    add(b'FUJI', 0, description)
    for kind, aux, data in out:
        add(kind, aux, data)
    open(out_path, 'wb').write(bytes(image))

    print("%s + %s -> %s" % (os.path.basename(model_path),
                             os.path.basename(xex_path),
                             os.path.basename(out_path)))
    print("   prologue %d + program %d = %d bytes in %d blocks (model had %d)"
          % (PROLOGUE, len(xex), len(payload), count, len(model)))


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__.strip().splitlines()[-1].strip())
    main(sys.argv[1], sys.argv[2], sys.argv[3])
