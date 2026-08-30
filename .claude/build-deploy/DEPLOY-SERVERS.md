# Build & Deploy — servers and deployment

Deployment notes and commands.

Deploy script (server and directory)

```bash
# server and directory (rebuilds ARM64, installs both units, restarts them)
MPH_SERVER_HOST=net.livetek.fr MPH_SERVER_USER=livetek \
  MPH_SERVER_PASS="$(read -rsp 'pi password: ' p; echo "$p")" ./deploy-server.sh
# MPH_DEPLOY_MASTER=0 to leave the directory alone
```

Publish commands (Windows client and server)

```bash
# Windows client
dotnet publish src/MphRead/MphRead.csproj -c Release -r win-x64 \
  --self-contained true -p:PublishSingleFile=true -o publish/win-x64

# Windows dedicated server
dotnet publish src/MphRead/MphRead.csproj -c Release -r win-x64 \
  -p:MphReadServer=true --self-contained true -p:PublishSingleFile=true \
  -o publish/win-x64-server
```

Notes

- The exe may be locked by a running game; write `MphRead.new.exe` then `mv`.
- Any protocol change requires server and every client to be the same build. `NetConfig.ProtocolVersion` is **4** in this build — a mismatched client is refused outright at Hello. Deploy the server before handing out a client built against a new version.
