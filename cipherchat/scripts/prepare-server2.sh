#!/usr/bin/env bash
# Fresh deployment authorized by the user; the old server is already closed.
set -euo pipefail
test "$(id -u)" = 0
release=/opt/cipherchat/releases/20260915-server2
test -x "$release/cipherchat-server"
test ! -e /var/lib/cipherchat
test ! -e /etc/systemd/system/cipherchat.service
umask 077
mkdir -m 0700 /var/lib/cipherchat
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 365 \
  -subj /CN=CipherChat -addext 'subjectAltName=IP:2603:1040:f02:d::21d' \
  -addext 'basicConstraints=critical,CA:FALSE' -addext 'keyUsage=critical,digitalSignature' \
  -addext 'extendedKeyUsage=serverAuth' -keyout /var/lib/cipherchat/key.pem -out /var/lib/cipherchat/cert.pem
openssl verify -CAfile /var/lib/cipherchat/cert.pem /var/lib/cipherchat/cert.pem
systemd-analyze verify "$release/cipherchat-server2.service"
(set -o noclobber; cat "$release/cipherchat-server2.service" > /etc/systemd/system/cipherchat.service)
chmod 644 /etc/systemd/system/cipherchat.service
systemctl daemon-reload
systemctl enable --now cipherchat.service
sleep 1
systemctl is-active cipherchat.service
systemctl is-enabled cipherchat.service
ss -lntp 'sport = :55220'
openssl x509 -in /var/lib/cipherchat/cert.pem -noout -fingerprint -sha256 -dates
