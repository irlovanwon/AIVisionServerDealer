# Server Guide — AIVisionServerDealer

## Local Design Folder

| Item | Value |
|------|-------|
| Folder | `~/AOC/Lib/AI/AIVisionServerDealer` |
| Purpose | Design documents (`.md` files, architecture specs) — **local only**, not deployed to remote devices |
| API Design Source | `~/AOC/Lib/AI/AIVisionServerDealer/API design/` |

Design documents must reside in the local design folder only and must not be deployed to remote/target devices. The remote device folder contains software development files only (source code, build artifacts, configurations, scripts, runtime assets). See [Coding/rule.md §4](../../Coding/rule.md).

---

## Remote Servers

The base server resources are defined in [../../../Server/server.md](../../../Server/server.md). This project deploys to both **EDGE01** and **EDGE02**.

### Server: EDGE01

| Item | Value |
|------|-------|
| Server Name | EDGE01 |
| IP Address | 100.85.117.73 |
| Protocol | SSH |
| Username | `user` |
| Password | `admin` |

#### Remote Directories

| Purpose | Path |
|---------|------|
| Project Folder | `/home/user/ECIDS/AIVisionServerDealer` |
| Configuration | `/home/user/ECIDS/AIVisionServerDealer/config/default.json` |
| TLS Certificates | `/home/user/ECIDS/AIVisionServerDealer/certs/` |

#### Quick Commands

```bash
ssh user@100.85.117.73
cd /home/user/ECIDS/AIVisionServerDealer
```

#### Service

```bash
cd /home/user/ECIDS/AIVisionServerDealer
./scripts/start.sh start              # Build (if needed) and start
./scripts/start.sh stop               # Graceful stop
./scripts/start.sh restart            # Restart
./scripts/start.sh status             # Check status
./scripts/start.sh build              # Build only
```

---

### Server: EDGE02

| Item | Value |
|------|-------|
| Server Name | EDGE02 |
| IP Address | 100.69.131.6 |
| Protocol | SSH |
| Username | `user` |
| Password | `admin` |

#### Remote Directories

| Purpose | Path |
|---------|------|
| Project Folder | `/home/user/ECIDS/AIVisionServerDealer` |
| Configuration | `/home/user/ECIDS/AIVisionServerDealer/config/default.json` |
| TLS Certificates | `/home/user/ECIDS/AIVisionServerDealer/certs/` |

#### Quick Commands

```bash
ssh user@100.69.131.6
cd /home/user/ECIDS/AIVisionServerDealer
```

#### Service

```bash
cd /home/user/ECIDS/AIVisionServerDealer
./scripts/start.sh start              # Build (if needed) and start
./scripts/start.sh stop               # Graceful stop
./scripts/start.sh restart            # Restart
./scripts/start.sh status             # Check status
./scripts/start.sh build              # Build only
```

---

## Git Repository

Follow the Git rules in [../../Git/development_rule.md](../../Git/development_rule.md). After any source code change, sync to GitHub (commit and push) to keep the remote repository up to date.

| Local Folder | Remote Folder (EDGE01/EDGE02) | GitHub Repo |
|-------------|-------------------------------|-------------|
| `AIVisionServerDealer` | `/home/user/ECIDS/AIVisionServerDealer` | https://github.com/irlovanwon/AIVisionServerDealer.git |

### Sync Workflow

```bash
# From the local repo
git add .
git commit -m "descriptive message"
git push origin main
```

---

## Server Resources Reference

Full server resources (all servers, credentials) are maintained centrally in [../../../Server/server.md](../../../Server/server.md). This file documents only the servers relevant to AIVisionServerDealer.

## Related Documents

| Document | Description |
|----------|-------------|
| [design.md](design.md) | System architecture & design |
| [folder_structure.md](folder_structure.md) | Project directory layout |
| [config.md](config.md) | Configuration design |
