#!/usr/bin/env bash
# Fresh deployment authorized by the user; the old server is already closed.
set -euo pipefail
test "$(id -u)" = 0
release=/opt/nextalk/releases/20260915-server2
test -x "$release/nextalk-server"
test ! -e /var/lib/nextalk
test ! -e /etc/systemd/system/nextalk.service
umask 077
mkdir -m 0700 /var/lib/nextalk
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 365 \
  -subj /CN=NEXTALK -addext 'subjectAltName=IP:2603:1040:f02:d::21d' \
  -addext 'basicConstraints=critical,CA:FALSE' -addext 'keyUsage=critical,digitalSignature' \
  -addext 'extendedKeyUsage=serverAuth' -keyout /var/lib/nextalk/key.pem -out /var/lib/nextalk/cert.pem
openssl verify -CAfile /var/lib/nextalk/cert.pem /var/lib/nextalk/cert.pem
systemd-analyze verify "$release/nextalk-server2.service"
(set -o noclobber; cat "$release/nextalk-server2.service" > /etc/systemd/system/nextalk.service)
chmod 644 /etc/systemd/system/nextalk.service
systemctl daemon-reload
systemctl enable --now nextalk.service
sleep 1
systemctl is-active nextalk.service
systemctl is-enabled nextalk.service
ss -lntp 'sport = :55220'
openssl x509 -in /var/lib/nextalk/cert.pem -noout -fingerprint -sha256 -dates
