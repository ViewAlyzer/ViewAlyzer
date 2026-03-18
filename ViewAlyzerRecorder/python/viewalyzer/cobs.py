"""
viewalyzer.cobs — COBS (Consistent Overhead Byte Stuffing) encoder/decoder.

COBS guarantees that 0x00 never appears in the encoded payload.
Each encoded frame is terminated with a 0x00 delimiter byte.
Overhead is ~1 byte per 254 input bytes.
"""


def cobs_encode(data: bytes) -> bytes:
    """COBS-encode *data* and append a 0x00 frame delimiter.

    Returns bytes guaranteed to contain no 0x00 values except the final
    delimiter byte.
    """
    out = bytearray()
    code_idx = len(out)
    out.append(0)       # placeholder for first code byte
    code = 1

    for b in data:
        if b != 0:
            out.append(b)
            code += 1
        else:
            out[code_idx] = code
            code_idx = len(out)
            out.append(0)   # placeholder for next code
            code = 1

        if code == 0xFF:
            out[code_idx] = code
            code_idx = len(out)
            out.append(0)   # placeholder
            code = 1

    out[code_idx] = code
    out.append(0x00)        # frame delimiter
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    """Decode a COBS-encoded buffer (without the trailing 0x00 delimiter).

    Raises ValueError on malformed input.
    """
    if len(data) == 0:
        return b""

    out = bytearray()
    idx = 0
    length = len(data)

    while idx < length:
        code = data[idx]
        if code == 0:
            raise ValueError("Unexpected zero byte in COBS data")
        idx += 1

        for _ in range(code - 1):
            if idx >= length:
                raise ValueError("COBS decode: ran past end of buffer")
            out.append(data[idx])
            idx += 1

        # Insert implicit zero between groups, but NOT after the final
        # group and NOT if code was 0xFF (block continuation).
        if code < 0xFF and idx < length:
            out.append(0)

    return bytes(out)
