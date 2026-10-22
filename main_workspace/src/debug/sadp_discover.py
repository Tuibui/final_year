#!/usr/bin/env python3
"""ค้นหากล้อง/อุปกรณ์ Hikvision ในวง LAN ด้วยโปรโตคอล SADP.

ทำงานแบบเดียวกับ SADP Tool ของ Hikvision: ส่ง multicast "inquiry"
ไปที่ 239.255.255.250:37020 แล้วฟังการตอบกลับจากอุปกรณ์ในเครือข่าย
ใช้แค่ standard library — ไม่ต้องลง pip package เพิ่ม

ตัวอย่างใช้งาน:
    python3 sadp_discover.py
    python3 sadp_discover.py --timeout 5
"""

import argparse
import socket
import struct
import uuid
import xml.etree.ElementTree as ET

SADP_MCAST_GRP = "239.255.255.250"
SADP_MCAST_PORT = 37020


def build_inquiry() -> bytes:
    """สร้าง payload XML สำหรับถามหาอุปกรณ์ (Probe/inquiry)."""
    probe_uuid = str(uuid.uuid4()).upper()
    xml = (
        '<?xml version="1.0" encoding="utf-8"?>'
        "<Probe>"
        f"<Uuid>{probe_uuid}</Uuid>"
        "<Types>inquiry</Types>"
        "</Probe>"
    )
    return xml.encode("utf-8")


def parse_response(data: bytes) -> dict | None:
    """แกะ XML ที่อุปกรณ์ตอบกลับมาเป็น dict ของฟิลด์ที่สนใจ."""
    try:
        root = ET.fromstring(data.decode("utf-8", errors="ignore"))
    except ET.ParseError:
        return None

    fields = (
        "DeviceDescription",
        "DeviceSN",
        "IPv4Address",
        "Port",
        "HttpPort",
        "MAC",
        "IPv4SubnetMask",
        "IPv4Gateway",
        "SoftwareVersion",
        "Activated",
    )
    info = {tag: (root.findtext(tag) or "") for tag in fields}
    # ถือว่าเป็นการตอบกลับที่ใช้ได้ก็ต่อเมื่อมี IP
    return info if info["IPv4Address"] else None


def discover(timeout: float = 4.0) -> list[dict]:
    """ส่ง inquiry แล้วเก็บอุปกรณ์ที่ตอบกลับภายในเวลา timeout วินาที."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    sock.bind(("", SADP_MCAST_PORT))

    # เข้าร่วม multicast group เพื่อรับการตอบกลับ
    mreq = struct.pack("4sl", socket.inet_aton(SADP_MCAST_GRP), socket.INADDR_ANY)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    sock.settimeout(timeout)

    sock.sendto(build_inquiry(), (SADP_MCAST_GRP, SADP_MCAST_PORT))

    found: dict[str, dict] = {}
    while True:
        try:
            data, _ = sock.recvfrom(8192)
        except socket.timeout:
            break
        info = parse_response(data)
        if info:
            found[info["MAC"] or info["IPv4Address"]] = info

    sock.close()
    return list(found.values())


def main() -> None:
    parser = argparse.ArgumentParser(description="ค้นหากล้อง Hikvision ด้วย SADP")
    parser.add_argument(
        "--timeout", type=float, default=4.0, help="เวลารอตอบกลับ (วินาที)"
    )
    args = parser.parse_args()

    print(f"กำลังค้นหาอุปกรณ์ Hikvision (รอ {args.timeout:.0f} วินาที)...\n")
    devices = discover(args.timeout)

    if not devices:
        print("ไม่พบอุปกรณ์ — ตรวจสอบว่าอยู่วง LAN เดียวกันและ firewall ไม่บล็อก UDP 37020")
        return

    for i, d in enumerate(devices, 1):
        active = "ใช่" if d["Activated"].lower() == "true" else "ยังไม่ได้ activate"
        print(f"[{i}] {d['DeviceDescription']}  (SN: {d['DeviceSN']})")
        print(f"    IP        : {d['IPv4Address']}  (mask {d['IPv4SubnetMask']})")
        print(f"    Gateway   : {d['IPv4Gateway']}")
        print(f"    MAC       : {d['MAC']}")
        print(f"    Port      : {d['Port']}   HTTP: {d['HttpPort']}")
        print(f"    Firmware  : {d['SoftwareVersion']}")
        print(f"    Activated : {active}")
        print(
            f"    RTSP      : rtsp://<user>:<pass>@{d['IPv4Address']}:554"
            "/Streaming/Channels/101\n"
        )

    print(f"พบทั้งหมด {len(devices)} อุปกรณ์")


if __name__ == "__main__":
    main()
