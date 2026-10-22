#!/usr/bin/env bash
# ตั้ง static IP บนพอร์ต LAN ของ Jetson เพื่อคุยกับ GC1000 (Modbus/TCP, พอร์ต 502)
# ใช้ NetworkManager (nmcli) -> ค่าอยู่ถาวรข้ามรีบูต + auto-connect เมื่อเสียบสาย
set -euo pipefail

# ===================== ปรับค่าตรงนี้ =====================
IFACE="auto"              # "auto" = ตรวจหาพอร์ต ethernet ให้เอง (Jetson มักเป็น eth0, dev box เป็น enp2s0)
                          #          หรือใส่ชื่อตรง ๆ เช่น "eth0" ถ้ารู้แน่
CON_NAME="gc1000"         # ชื่อ connection profile
PC_IP="192.168.0.100"     # IP ของ Jetson (ต้องคนละเลขกับ GC1000)
PREFIX="24"               # /24 = netmask 255.255.255.0
GC1000_IP="192.168.0.10"  # IP ของ GC1000 (ตาม gc_modbus.yaml) ใช้ ping เทสต์
# ========================================================

# --- auto-detect พอร์ต ethernet ถ้า IFACE=auto (รองรับทั้ง Jetson eth0 และ dev box enp2s0) ---
if [ "${IFACE}" = "auto" ]; then
  IFACE="$(nmcli -t -f DEVICE,TYPE device status | awk -F: '$2=="ethernet"{print $1; exit}')"
  if [ -z "${IFACE}" ]; then
    echo "!! หาพอร์ต ethernet ไม่เจอ — ดูรายการด้านล่าง แล้วแก้ตัวแปร IFACE ในสคริปต์เอง:"
    nmcli device status
    exit 1
  fi
  echo ">> auto-detect พอร์ต LAN = ${IFACE}"
fi

echo ">> ตั้ง static IP ${PC_IP}/${PREFIX} บน ${IFACE}  (profile: ${CON_NAME})"

# ลบ profile เดิมถ้ามี กันค่าซ้อน
if nmcli -t -f NAME con show | grep -qx "${CON_NAME}"; then
  echo ">> ลบ profile เดิม '${CON_NAME}'"
  sudo nmcli con delete "${CON_NAME}"
fi

# สร้าง profile ใหม่
#  - ipv4.method manual            = static
#  - ipv4.never-default yes        = ห้ามแย่ง default route ของ WiFi (เน็ตออฟฟิศยังใช้ได้ปกติ)
#  - ไม่ตั้ง gateway/DNS           = ลิงก์ตรง ไม่ต้องมี
sudo nmcli con add type ethernet ifname "${IFACE}" con-name "${CON_NAME}" \
  ipv4.method manual \
  ipv4.addresses "${PC_IP}/${PREFIX}" \
  ipv4.never-default yes \
  ipv6.method ignore \
  connection.autoconnect yes

echo ">> activate profile"
if sudo nmcli con up "${CON_NAME}"; then
  echo ">> ขึ้นเรียบร้อย"
else
  echo "!! ยัง activate ไม่ได้ — ส่วนใหญ่เพราะสาย LAN ยังไม่ได้เสียบ (NO-CARRIER)"
  echo "   เสียบสายระหว่าง Jetson <-> GC1000 แล้วมันจะ auto-connect เอง"
fi

echo
echo ">> สถานะ ${IFACE}:"
ip -4 addr show "${IFACE}" || true

echo
echo ">> ทดสอบ ping GC1000 (${GC1000_IP}) ..."
if ping -c 3 -W 2 "${GC1000_IP}"; then
  echo ">> OK คุยกับ GC1000 ได้แล้ว — รันไดรเวอร์ได้เลย"
else
  echo "!! ping ไม่ติด เช็ค: (1) เสียบสายหรือยัง  (2) IP ฝั่ง GC1000 = ${GC1000_IP} จริงไหม  (3) ไฟลิงก์ที่พอร์ต"
fi
