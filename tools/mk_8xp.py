#!/usr/bin/env python3
"""Write a TI-BASIC program as a .8xp file (tests).

    tools/mk_8xp.py OUT.8xp NAME 'tokens as hex' [--archived]
e.g. Disp "HELLO N84":  tools/mk_8xp.py hello.8xp HELLO 'DE 2A 48 45 4C 4C 4F 29 4E 38 34 2A'
"""
import struct, sys

out, name, hexdata = sys.argv[1], sys.argv[2].encode(), bytes.fromhex(sys.argv[3])
archived = '--archived' in sys.argv
vardata = struct.pack('<H', len(hexdata)) + hexdata
entry = struct.pack('<HHB', 0x0D, len(vardata), 0x05) + name.ljust(8, b'\0')[:8] + \
        bytes([0, 0x80 if archived else 0]) + struct.pack('<H', len(vardata)) + vardata
header = b'**TI83F*\x1a\x0a\x00' + b'N84 test program'.ljust(42, b'\0') + struct.pack('<H', len(entry))
open(out, 'wb').write(header + entry + struct.pack('<H', sum(entry) & 0xFFFF))
