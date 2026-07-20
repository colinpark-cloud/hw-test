#!/bin/bash
# hw-test 보드 배포 스크립트
# OS 재이미징 후 실행: ./deploy.sh [IP]
# 기본 IP: 192.168.1.100

set -e

BOARD_IP="${1:-192.168.1.100}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== hw-test 보드 배포 ==="
echo "대상: root@$BOARD_IP"

# host key 초기화
ssh-keygen -f "$HOME/.ssh/known_hosts" -R "$BOARD_IP" 2>/dev/null || true
rm -f "$HOME/.ssh/known_hosts.old"

# 보드 연결 대기
echo "보드 연결 대기 중..."
until ssh -o StrictHostKeyChecking=no -o ConnectTimeout=3 root@"$BOARD_IP" 'echo ok' 2>/dev/null; do
    sleep 3
done
echo "연결됨"

# 파일 배포
echo "파일 배포 중..."
ssh -o StrictHostKeyChecking=no root@"$BOARD_IP" 'rm -f /mnt/data/home/user/hw-test /mnt/data/home/user/hmi-test'
scp "$HOME/weston-mirror/build-aarch64/mirror-output.so" root@"$BOARD_IP":/usr/lib/weston/ &
scp "$SCRIPT_DIR/build-aarch64/hw-test" root@"$BOARD_IP":/mnt/data/home/user/hw-test &
scp "$HOME/hmi-test/build-aarch64/hmi-test" root@"$BOARD_IP":/mnt/data/home/user/hmi-test &
scp "$SCRIPT_DIR/assets/left.wav" "$SCRIPT_DIR/assets/right.wav" root@"$BOARD_IP":/usr/share/hw-test/ &
wait
echo "파일 배포 완료"

# 설정
echo "설정 중..."
ssh -o StrictHostKeyChecking=no root@"$BOARD_IP" 'bash -s' << 'ENDSSH'
set -e

cat > /etc/systemd/system/hw-test.service << 'EOF'
[Unit]
Description=HW Test Dashboard
After=weston.service
Requires=weston.service

[Service]
User=root
Environment=QT_QPA_PLATFORM=wayland
Environment=WAYLAND_DISPLAY=wayland-1
Environment=XDG_RUNTIME_DIR=/run/user/1201
ExecStartPre=/bin/sh -c 'echo 255 > /sys/class/backlight/backlight-lvds/brightness; echo on > /sys/class/backlight/backlight-lvds/power/control'
ExecStartPre=/bin/sleep 8
ExecStart=/mnt/data/home/user/hmi-test
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

cat > /etc/udev/rules.d/99-backlight.rules << 'EOF'
SUBSYSTEM=="backlight", KERNEL=="backlight-lvds", ACTION=="add", RUN+="/bin/sh -c 'echo 255 > /sys%p/brightness; echo on > /sys%p/power/control'"
EOF
udevadm control --reload-rules

systemctl mask systemd-backlight@backlight:backlight-lvds.service 2>/dev/null || true
systemctl enable sshd
systemctl enable chronyd && systemctl start chronyd
chmod +x /mnt/data/home/user/hw-test /mnt/data/home/user/hmi-test
mkdir -p /usr/share/hw-test

# 네트워크 고정 IP (LAN1=192.168.1.100, LAN2=192.168.2.100)
mkdir -p /etc/NetworkManager/system-connections
cat > /etc/NetworkManager/system-connections/eth0.nmconnection << 'EOF'
[connection]
id=eth0
type=ethernet
interface-name=eth0

[ethernet]

[ipv4]
address1=192.168.1.100/24,192.168.1.1
method=manual

[ipv6]
method=disabled
EOF
cat > /etc/NetworkManager/system-connections/eth1.nmconnection << 'EOF'
[connection]
id=eth1
type=ethernet
interface-name=eth1

[ethernet]

[ipv4]
address1=192.168.2.100/24
method=manual

[ipv6]
method=disabled
EOF
chmod 600 /etc/NetworkManager/system-connections/eth0.nmconnection
chmod 600 /etc/NetworkManager/system-connections/eth1.nmconnection
nmcli connection reload 2>/dev/null || true
systemctl daemon-reload
systemctl enable hw-test

cat > /etc/xdg/weston/weston.ini << 'EOF'
[core]
repaint-window=16
idle-time=0
modules=screen-share.so,mirror-output.so
shell=kiosk
[libinput]
touchscreen_calibrator=true
[shell]
close-animation=none
startup-animation=none
panel-position=none
background-color=0xFF000000
[output]
name=LVDS-1
transform=normal
[output]
name=HDMI-A-1
mode=preferred
[keyboard]
vt-switching=false
[screen-share]
command=/usr/bin/weston --backend=vnc-backend.so --shell=fullscreen-shell.so --vnc-tls-cert=/etc/remote/keys/tls.crt --vnc-tls-key=/etc/remote/keys/tls.key
start-on-startup=false
EOF

echo "설정 완료"
ENDSSH

# 시간 동기화
KST_TIME=$(TZ=Asia/Seoul date +"%Y-%m-%d %H:%M:%S")
ssh -o StrictHostKeyChecking=no root@"$BOARD_IP" "date -s '$KST_TIME' && hwclock -w"
echo "시간 동기화 완료"

# weston + hw-test 시작
ssh -o StrictHostKeyChecking=no root@"$BOARD_IP" 'systemctl restart weston'
echo "weston 재시작, 15초 대기..."
sleep 15
ssh -o StrictHostKeyChecking=no root@"$BOARD_IP" 'systemctl start hw-test'

echo ""
echo "=== 배포 완료 ==="
