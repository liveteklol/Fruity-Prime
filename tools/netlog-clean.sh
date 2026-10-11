#!/bin/sh
# Empty every Fruity Prime netlog (netlog-*.txt) on this box.
#
# A server writes one for as long as it runs and nothing ever shortens it:
# they reached a gigabyte a piece on the relays and on the Pi. Run daily by
# tools/systemd/fruityprime-netlog-clean.timer; installed as
# /usr/local/sbin/fruityprime-netlog-clean:
#
#   sudo install -m 0755 tools/netlog-clean.sh /usr/local/sbin/fruityprime-netlog-clean
#   sudo cp tools/systemd/fruityprime-netlog-clean.{service,timer} /etc/systemd/system/
#   sudo systemctl daemon-reload && sudo systemctl enable --now fruityprime-netlog-clean.timer
#
# The files are emptied, not deleted: the server keeps writing to the one it
# has open, and a deleted file would hold its space until the next restart.
# A C++ server appends, so the file starts again from zero. The C# v0.10.0
# one writes on at its old offset, so `ls` still shows the old length, but
# the disk space is freed (a sparse file; `du` shows what it holds).
set -u

for root in /opt /home; do
  find "$root" -xdev -maxdepth 4 -type f -name 'netlog-*.txt' -size +0 \
    \( -ipath '*fruity*' -o -ipath '*mphread*' \) 2>/dev/null
done | while IFS= read -r file; do
  held=$(du -k "$file" | cut -f1)
  if truncate -s 0 "$file"; then
    echo "emptied $file (${held} KB)"
  fi
done
