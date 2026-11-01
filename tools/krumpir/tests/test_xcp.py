"""EcuPort without hardware: the A2L parser on a small A2L, and the XCP master against a fake
ECU (XCP on UDP in a thread, with a flat memory). The test against the real board is
test_ecu_board.py."""
import socket
import struct
import threading
from pathlib import Path

import pytest

from krumpir.xcp import A2L, EcuPort, XcpError

A2L_FILE = Path(__file__).parent / "data" / "mini.a2l"


class FakeEcu:
    """Answers CONNECT, DISCONNECT, SHORT_UPLOAD, SHORT_DOWNLOAD, SET_MTA, DOWNLOAD."""

    def __init__(self, epk="mini_build_1", max_cto=32):
        self.mem = bytearray(0x4000)
        self.epk, self.max_cto, self.mta = epk.encode(), max_cto, 0
        self.log = []
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.port = self.sock.getsockname()[1]
        threading.Thread(target=self._serve, daemon=True).start()

    def _read(self, ext, addr, n):
        if addr == 0xFFFFFF00:
            return self.epk[:n]
        if ext != 0:
            raise IndexError
        return bytes(self.mem[addr:addr + n])

    def _answer(self, p):
        cmd = p[0]
        self.log.append(cmd)
        if cmd == 0xFF:
            return bytes([0xFF, 0x15, 0x00, self.max_cto, 0, 4, 1, 1])
        if cmd == 0xFE:
            return b"\xff"
        if cmd == 0xF4:  # SHORT_UPLOAD
            n, ext, addr = p[1], p[3], struct.unpack_from("<I", p, 4)[0]
            return b"\xff" + self._read(ext, addr, n)
        if cmd == 0xED:  # SHORT_DOWNLOAD
            n, addr = p[1], struct.unpack_from("<I", p, 4)[0]
            self.mem[addr:addr + n] = p[8:8 + n]
            return b"\xff"
        if cmd == 0xF6:  # SET_MTA
            self.mta = struct.unpack_from("<I", p, 4)[0]
            return b"\xff"
        if cmd == 0xF0:  # DOWNLOAD
            n = p[1]
            self.mem[self.mta:self.mta + n] = p[2:2 + n]
            self.mta += n
            return b"\xff"
        return b"\xfe\x20"  # CMD_UNKNOWN

    def _serve(self):
        while True:
            msg, peer = self.sock.recvfrom(2048)
            n, ctr = struct.unpack_from("<HH", msg)
            p = msg[4:4 + n]
            try:
                reply = self._answer(p)
            except IndexError:
                reply = b"\xfe\x22"  # OUT_OF_RANGE
            event = struct.pack("<HH", 2, 0) + b"\xfd\x00"  # an event before the answer
            self.sock.sendto(event + struct.pack("<HH", len(reply), ctr) + reply, peer)


@pytest.fixture
def fake():
    return FakeEcu()


def test_a2l_symbols():
    a = A2L(A2L_FILE)
    assert (a.host, a.port, a.epk, a.epk_addr) == ("127.0.0.1", 5555, "mini_build_1", 0xFFFFFF00)
    s = a.symbols
    assert (s["cal_page.setpoint"].addr, s["cal_page.setpoint"].fmt) == (0x1000, "f")
    assert (s["cal_page.mode"].addr, s["cal_page.mode"].fmt) == (0x1004, "B")
    assert (s["s_loop.inner.gains"].addr, s["s_loop.inner.gains"].count) == (0x2008, 3)
    assert s["task.local.temp"].ext == 2 and s["s_count"].fmt == "I"
    assert s["s_buf"].size == 600


def test_read_and_write(fake):
    with EcuPort(A2L_FILE, port=fake.port) as ecu:
        assert ecu.max_cto == 32 and "cal_page.setpoint" in ecu.symbols()
        ecu.write({"cal_page.setpoint": 300.0, "cal_page.mode": 2})
        ecu.write(s_count=7)
        assert ecu.read("cal_page.setpoint") == 300.0
        assert ecu.read("cal_page.mode", "s_count") == {"cal_page.mode": 2, "s_count": 7}

        ecu.write({"s_loop.inner.gains": [1.5, -2.0, 3.25]})  # 24 bytes: one SHORT_DOWNLOAD
        assert ecu.read("s_loop.inner.gains") == [1.5, -2.0, 3.25]
        assert 0xF6 not in fake.log

        data = [i % 256 for i in range(600)]  # SET_MTA + DOWNLOADs, several SHORT_UPLOADs
        ecu.write({"s_buf": data})
        assert ecu.read("s_buf") == data
        assert 0xF6 in fake.log and fake.log.count(0xF0) == 20  # 600 / (32 - 2)

        with pytest.raises(XcpError, match="not in the A2L"):
            ecu.read("nope")
        with pytest.raises(XcpError, match="takes 3 values"):
            ecu.write({"s_loop.inner.gains": [1.0]})
        with pytest.raises(XcpError, match="OUT_OF_RANGE"):
            ecu.read("task.local.temp")  # the fake has no event-relative memory
    assert fake.log[-1] == 0xFE  # disconnected


def test_epk_mismatch():
    fake = FakeEcu(epk="other_build_")
    with pytest.raises(XcpError, match="EPK mismatch"):
        EcuPort(A2L_FILE, port=fake.port)
    with EcuPort(A2L_FILE, port=fake.port, check_epk=False) as ecu:
        assert ecu.read("s_count") == 0


def test_no_answer():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))  # bound, never answers
    with pytest.raises(XcpError, match="no answer"):
        EcuPort(A2L_FILE, port=s.getsockname()[1], timeout=0.2)
    s.close()
