#!/usr/bin/env bash
# Run on the target host after approval to enable the service and its IPv6 port.
set -euo pipefail
release=/home/linuxuser/cipherchat/releases/20260914-ipv6-1
service=/etc/systemd/system/cipherchat.service
address=2401:c080:1000:5916:5400:6ff:feac:a0b3
test "$(id -un)" = linuxuser
test -x "$release/bin/cipherchat-server"
test -s /home/linuxuser/cipherchat/data/cert.pem
test -s /home/linuxuser/cipherchat/data/key.pem
sudo -n test ! -e "$service"
systemd-analyze verify "$release/scripts/cipherchat-linux.service"
# Refuse to overwrite an existing unit, including one created during this check.
sudo -n bash -c 'set -o noclobber; cat > /etc/systemd/system/cipherchat.service' < "$release/scripts/cipherchat-linux.service"
sudo -n systemctl daemon-reload
sudo -n systemctl enable --now cipherchat.service
sudo -n systemctl is-active --quiet cipherchat.service
sudo -n ufw allow in proto tcp to "$address" port 55220 comment 'CipherChat TLS IPv6'
sudo -n systemctl is-enabled cipherchat.service
sudo -n systemctl is-active cipherchat.service
sudo -n ss -lntp 'sport = :55220'
sudo -n ufw status numbered
