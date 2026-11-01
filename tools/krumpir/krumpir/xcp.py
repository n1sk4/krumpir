"""EcuPort: measurement and calibration of a controller over XCP on UDP (XIL: ECUMPort and
ECUCPort), with the addresses and types from its A2L file.

A minimal XCP master: CONNECT, an EPK check against the A2L, and polling reads and writes of
memory (SHORT_UPLOAD / SHORT_DOWNLOAD, SET_MTA + DOWNLOAD for long values). Enough for reading
controller state and setting calibration values from a test; DAQ (synchronous recording) is not
implemented: record with xcpclient for that.

    with EcuPort("docs/t12.a2l") as ecu:          # host and port from the A2L
        ecu.write({"t12_cal_page.setpoint": 300})
        print(ecu.read("s_loop.temp"))
"""
import re
import socket
import struct

# A2L datatype -> struct format (little endian: BYTE_ORDER MSB_LAST)
DATATYPES = {"UBYTE": "B", "SBYTE": "b", "UWORD": "H", "SWORD": "h", "ULONG": "I", "SLONG": "i",
             "A_UINT64": "Q", "A_INT64": "q", "FLOAT32_IEEE": "f", "FLOAT64_IEEE": "d"}

ERRORS = {0x10: "CMD_BUSY", 0x11: "DAQ_ACTIVE", 0x12: "PGM_ACTIVE", 0x20: "CMD_UNKNOWN",
          0x21: "CMD_SYNTAX", 0x22: "OUT_OF_RANGE", 0x23: "WRITE_PROTECTED",
          0x24: "ACCESS_DENIED", 0x25: "ACCESS_LOCKED", 0x26: "PAGE_NOT_VALID",
          0x27: "MODE_NOT_VALID", 0x28: "SEGMENT_NOT_VALID", 0x29: "SEQUENCE", 0x2A: "DAQ_CONFIG",
          0x30: "MEMORY_OVERFLOW", 0x31: "GENERIC", 0x32: "VERIFY"}


class XcpError(RuntimeError):
    """The ECU refused a command, did not answer, or does not match the A2L."""


class Symbol:
    """A measurement or calibration value: address, address extension, struct format, count."""

    def __init__(self, addr, ext, fmt, count=1):
        self.addr, self.ext, self.fmt, self.count = addr, ext, fmt, count

    @property
    def size(self):
        return struct.calcsize("<" + self.fmt) * self.count

    def __repr__(self):
        return f"Symbol(0x{self.addr:X}, ext={self.ext}, {self.fmt}x{self.count})"


def _tokens(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.findall(r'"(?:[^"\\]|\\.)*"|\S+', text)


def _blocks(tokens, i=0):
    """Parses /begin KIND args... /end KIND into [(kind, args, children)]. Returns the blocks,
    the loose tokens beside them, and the index after the enclosing /end."""
    out, loose = [], []
    while i < len(tokens):
        t = tokens[i]
        if t == "/end":
            return out, loose, i + 2
        if t == "/begin":
            children, rest, j = _blocks(tokens, i + 2)
            out.append((tokens[i + 1], rest, children))
            i = j
        else:
            loose.append(t)
            i += 1
    return out, loose, i


def _walk(blocks):
    for b in blocks:
        yield b
        yield from _walk(b[2])


def _keyword(args, key, conv=str, default=None):
    return conv(args[args.index(key) + 1]) if args and key in args else default


def _int(s):
    return int(s, 0)


def _dims(args):
    if "MATRIX_DIM" not in args:
        return 1
    n, i = 1, args.index("MATRIX_DIM") + 1
    while i < len(args) and re.fullmatch(r"\d+", args[i]):
        n *= int(args[i])
        i += 1
    return n


class A2L:
    """The parts of an A2L that EcuPort needs (as xcpclient writes them for XCPlite)."""

    def __init__(self, path):
        with open(path, encoding="utf-8", errors="replace") as f:
            blocks = list(_walk(_blocks(_tokens(f.read()))[0]))
        mod_par = next((a for k, a, _ in blocks if k == "MOD_PAR"), [])
        self.epk = _keyword(mod_par, "EPK", lambda s: s.strip('"'))
        self.epk_addr = _keyword(mod_par, "ADDR_EPK", _int)
        udp = next((a for k, a, _ in blocks if k == "XCP_ON_UDP_IP"), None)
        self.port = _int(udp[1]) if udp else 5555
        self.host = _keyword(udp, "ADDRESS", lambda s: s.strip('"'))

        layouts = {a[0]: DATATYPES[a[3]] for k, a, _ in blocks
                   if k == "RECORD_LAYOUT" and len(a) > 3 and a[3] in DATATYPES}
        types = {}  # typedef -> (fmt, count), or [(component, typedef, offset, count)] for structs
        for kind, a, children in blocks:
            if kind == "TYPEDEF_MEASUREMENT" and a[2] in DATATYPES:
                types[a[0]] = (DATATYPES[a[2]], _dims(a))
            elif kind == "TYPEDEF_CHARACTERISTIC" and a[3] in layouts:
                types[a[0]] = (layouts[a[3]], _dims(a))
            elif kind == "TYPEDEF_STRUCTURE":
                types[a[0]] = [(c[0], c[1], _int(c[2]), _dims(c)) for k, c, _ in children
                               if k == "STRUCTURE_COMPONENT"]

        self.symbols = {}

        def add(name, typedef, addr, ext, count=1):
            t = types.get(typedef)
            if isinstance(t, tuple):
                self.symbols[name] = Symbol(addr, ext, t[0], t[1] * count)
            elif isinstance(t, list) and count == 1:  # ponytail: arrays of structs not flattened
                for comp, ctype, offset, ccount in t:
                    add(f"{name}.{comp}", ctype, addr + offset, ext, ccount)

        for kind, a, _ in blocks:
            ext = _keyword(a, "ECU_ADDRESS_EXTENSION", _int, 0)
            if kind == "MEASUREMENT" and a[2] in DATATYPES:
                self.symbols[a[0]] = Symbol(_keyword(a, "ECU_ADDRESS", _int), ext,
                                            DATATYPES[a[2]], _dims(a))
            elif kind == "CHARACTERISTIC" and a[4] in layouts:
                self.symbols[a[0]] = Symbol(_int(a[3]), ext, layouts[a[4]], _dims(a))
            elif kind == "INSTANCE":
                add(a[0], a[2], _int(a[3]), ext, _dims(a))


class EcuPort:
    """Polling measurement and calibration of an ECU over XCP on UDP (XIL: ECUMPort, ECUCPort).

    Names are those of the A2L, with struct members joined by dots (t12_cal_page.setpoint,
    s_loop.temp). Values in calibration segments are written to the working page."""

    def __init__(self, a2l, host=None, port=None, timeout=1.0, check_epk=True):
        self.a2l = a2l if isinstance(a2l, A2L) else A2L(a2l)
        host = host or self.a2l.host
        if not host:
            raise XcpError("no host given and none in the A2L")
        self._addr = (host, port or self.a2l.port)
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.settimeout(timeout)
        self._ctr = 0
        try:
            r = self._cmd(0xFF, 0x00)  # CONNECT, normal mode
            if r[2] & 0x01:
                raise XcpError("the ECU is big endian; only little endian is supported")
            self.max_cto = r[3]
            if check_epk and self.a2l.epk:
                epk = self._upload(self.a2l.epk_addr, 0, len(self.a2l.epk)).decode(errors="replace")
                if epk != self.a2l.epk:
                    raise XcpError(f"EPK mismatch: the ECU runs '{epk}', the A2L is for "
                                   f"'{self.a2l.epk}' (regenerate the A2L for this build)")
        except BaseException:
            self.close()
            raise

    def _cmd(self, *packet):
        """Sends one command; returns the positive response. Skips events and DAQ packets."""
        self._ctr = (self._ctr + 1) & 0xFFFF
        data = bytes(packet)
        self._sock.sendto(struct.pack("<HH", len(data), self._ctr) + data, self._addr)
        while True:
            try:
                msg = self._sock.recv(65536)
            except socket.timeout:
                raise XcpError(f"no answer from {self._addr[0]}:{self._addr[1]} "
                               f"(command 0x{packet[0]:02X})") from None
            pos = 0
            while pos + 4 <= len(msg):  # one datagram may carry several packets
                n = struct.unpack_from("<H", msg, pos)[0]
                pkt, pos = msg[pos + 4:pos + 4 + n], pos + 4 + n
                if pkt[:1] == b"\xff":
                    return pkt
                if pkt[:1] == b"\xfe":
                    code = pkt[1] if len(pkt) > 1 else 0
                    raise XcpError(f"command 0x{packet[0]:02X} refused: "
                                   f"{ERRORS.get(code, f'0x{code:02X}')}")
                # 0xFD event, 0xFC service request, below: DAQ; not an answer

    def _upload(self, addr, ext, size):
        out = b""
        while len(out) < size:
            n = min(size - len(out), self.max_cto - 1)
            r = self._cmd(0xF4, n, 0, ext, *struct.pack("<I", addr + len(out)))  # SHORT_UPLOAD
            out += r[1:1 + n]
        return out

    def _download(self, addr, ext, data):
        if len(data) <= self.max_cto - 8:
            self._cmd(0xED, len(data), 0, ext, *struct.pack("<I", addr), *data)  # SHORT_DOWNLOAD
            return
        self._cmd(0xF6, 0, 0, ext, *struct.pack("<I", addr))  # SET_MTA
        for i in range(0, len(data), self.max_cto - 2):
            chunk = data[i:i + self.max_cto - 2]
            self._cmd(0xF0, len(chunk), *chunk)  # DOWNLOAD

    def symbol(self, name):
        try:
            return self.a2l.symbols[name]
        except KeyError:
            raise XcpError(f"'{name}' is not in the A2L") from None

    def symbols(self):
        """Every readable name (XIL: VariableNames)."""
        return sorted(self.a2l.symbols)

    def read(self, *names):
        """One name: its value (a list for arrays). Several: {name: value} (XIL: Read)."""
        values = {}
        for name in names:
            s = self.symbol(name)
            v = struct.unpack(f"<{s.count}{s.fmt}", self._upload(s.addr, s.ext, s.size))
            values[name] = v[0] if s.count == 1 else list(v)
        return values[names[0]] if len(names) == 1 else values

    def write(self, values=None, /, **kw):
        """Writes values by name, as a dict (dotted names) or keywords (XIL: Write)."""
        for name, value in {**(values or {}), **kw}.items():
            s = self.symbol(name)
            v = list(value) if isinstance(value, (list, tuple)) else [value]
            if len(v) != s.count:
                raise XcpError(f"'{name}' takes {s.count} values")
            if s.fmt not in "fd":
                v = [int(x) for x in v]
            self._download(s.addr, s.ext, struct.pack(f"<{s.count}{s.fmt}", *v))

    def close(self):
        if self._sock.fileno() >= 0:
            try:
                self._cmd(0xFE)  # DISCONNECT, so the next master can connect
            except (XcpError, OSError):
                pass
            self._sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
