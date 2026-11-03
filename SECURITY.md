# Security policy

## Supported versions

Only the latest commit on `main` is supported. There are no maintained release branches yet.

| Version | Supported |
|---------|-----------|
| `main`  | ✅ |
| older commits / tags | ❌ |

## Reporting a vulnerability

**Please do not open a public issue for security problems.**

Use GitHub's private reporting flow instead:

1. Go to the repository's **Security** tab → **Report a vulnerability**, or open
   <https://github.com/siravidthongsook/makhosfish/security/advisories/new>.
2. Include the affected version/commit, a minimal reproducer, and the impact you expect.

You can expect an acknowledgement within a few days. Once a fix is ready, a GitHub Security
Advisory is published with credit to the reporter unless you ask to stay anonymous.

## Scope

MakHosFish is a local engine; the attack surface is input parsing, not the network:

- **In scope** — memory-safety bugs (out-of-bounds reads/writes, UB, unbounded allocation) reachable
  from untrusted input: FEN strings, PDN game files, TCI protocol lines, and `.mhpw` weight files.
  Crash-proofing is especially important because GUIs and the web bridge feed these directly.
- **In scope (web bridge)** — request handling in `web/server/`, which binds to loopback only.
- **Out of scope** — engine playing strength, denial of service on inputs you construct yourself
  (e.g. an absurdly deep `perft`/`go`), and issues that require editing the source or the model
  weights directly.

## Hardening notes

- CI builds and tests on Ubuntu and macOS with ASan + UBSan enabled.
- Parsers are expected to reject malformed input instead of crashing or reading past the board;
  report any input that violates that expectation.
- The web bridge is a convenience for local play: it listens on `127.0.0.1`, spawns one engine
  child per WebSocket session, and must not be exposed to the public internet.
