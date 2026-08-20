#!/usr/bin/env bash
# Build the ARM64 dedicated server and push it to the Pi.
#
# The service is stopped before the binary is replaced: systemd holds the
# executable open while it runs, so overwriting it in place fails.
#
# Credentials come from the environment, not this file:
#   MPH_SERVER_HOST=france-mining.com MPH_SERVER_USER=livetek ./deploy-server.sh
# With an SSH key installed, no password is needed at all -- which is the
# setup worth moving to.
set -euo pipefail

HOST="${MPH_SERVER_HOST:-france-mining.com}"
USER="${MPH_SERVER_USER:-livetek}"
REMOTE_DIR="${MPH_SERVER_DIR:-/home/$USER/mphread-server}"
SERVICE="mphread-server"
# The same binary also runs the server directory the launcher's browser asks.
# One upload, two units; set MPH_DEPLOY_MASTER=0 to leave the directory alone.
MASTER_SERVICE="mphread-master"
DEPLOY_MASTER="${MPH_DEPLOY_MASTER:-1}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT="$ROOT/src/MphRead"
STAGE="$ROOT/publish/server-arm64"

# sshpass is only used when a password is supplied; a key-based setup skips it.
ssh_run() {
  if [ -n "${MPH_SERVER_PASS:-}" ]; then
    sshpass -p "$MPH_SERVER_PASS" ssh -o StrictHostKeyChecking=no "$USER@$HOST" "$@"
  else
    ssh -o StrictHostKeyChecking=no "$USER@$HOST" "$@"
  fi
}

scp_put() {
  if [ -n "${MPH_SERVER_PASS:-}" ]; then
    sshpass -p "$MPH_SERVER_PASS" scp -o StrictHostKeyChecking=no "$1" "$USER@$HOST:$2"
  else
    scp -o StrictHostKeyChecking=no "$1" "$USER@$HOST:$2"
  fi
}

echo "==> building linux-arm64"
dotnet publish "$PROJECT" -c Release -r linux-arm64 \
  --self-contained true -p:PublishSingleFile=true -o "$STAGE" \
  | grep -E "error|-> " || true
test -f "$STAGE/MphRead" || { echo "build produced no binary" >&2; exit 1; }

# Install a unit the first time, and leave a hand-edited one alone after that:
# an operator who changed the server name or the port on the box should not
# have it overwritten by a deploy.
install_unit() {
  local name="$1" template="$ROOT/tools/systemd/$1.service"
  if ssh_run "test -f /etc/systemd/system/$name.service"; then
    return 0
  fi
  echo "==> installing $name.service"
  sed -e "s|__USER__|$USER|g" -e "s|__DIR__|$REMOTE_DIR|g" "$template" \
    | ssh_run "cat > /tmp/$name.service"
  ssh_run "sudo mv /tmp/$name.service /etc/systemd/system/$name.service \
    && sudo systemctl daemon-reload && sudo systemctl enable $name"
}

echo "==> stopping $SERVICE"
ssh_run "sudo systemctl stop $SERVICE" || true
if [ "$DEPLOY_MASTER" = "1" ]; then
  ssh_run "sudo systemctl stop $MASTER_SERVICE" || true
fi

echo "==> uploading"
scp_put "$STAGE/MphRead" "$REMOTE_DIR/MphRead.new"
ssh_run "chmod +x $REMOTE_DIR/MphRead.new && mv $REMOTE_DIR/MphRead.new $REMOTE_DIR/MphRead"

install_unit "$SERVICE"
if [ "$DEPLOY_MASTER" = "1" ]; then
  install_unit "$MASTER_SERVICE"
fi

echo "==> starting $SERVICE"
ssh_run "sudo systemctl start $SERVICE"
if [ "$DEPLOY_MASTER" = "1" ]; then
  echo "==> starting $MASTER_SERVICE"
  ssh_run "sudo systemctl start $MASTER_SERVICE"
fi
sleep 3
ssh_run "systemctl is-active $SERVICE && journalctl -u $SERVICE -n 5 --no-pager | tail -4"
if [ "$DEPLOY_MASTER" = "1" ]; then
  ssh_run "systemctl is-active $MASTER_SERVICE \
    && journalctl -u $MASTER_SERVICE -n 5 --no-pager | tail -4"
fi

echo "==> done"
echo
echo "The browser in the launcher asks net.livetek.fr:27889 by default."
echo "That name has to resolve to this machine, and UDP 27889 has to reach it,"
echo "before any server shows up in anybody's list."
