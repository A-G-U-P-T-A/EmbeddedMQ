# Publishing clients to registries

One tag drives GitHub Release assets. Registry uploads run only when the
matching secrets exist on the repo.

## Release command

```bash
# 1) refresh vendored C
python scripts/sync_native.py

# 2) commit if needed, then tag the monorepo release + Go nested module
git tag -a v1.0.1-beta -m "EmbeddedMQ 1.0.1-beta"
git tag -a bindings/go/v1.0.0-beta.1 -m "Go module emq 1.0.0-beta.1"
git push origin v1.0.1-beta bindings/go/v1.0.0-beta.1
```

`Release bindings` workflow builds natives/JAR/wheels and publishes where secrets allow.

| Consumer install | Registry | Secret gate |
| ---------------- | -------- | ----------- |
| GitHub Release ZIP/JAR/sdist | GitHub | always (on tag `v*`) |
| `go get …/bindings/go@v1.0.0-beta.1` | proxy.golang.org | **no secrets** — nested tag `bindings/go/v…` |
| `pip install embeddedmq` | PyPI | `PYPI_API_TOKEN` |
| `implementation("io.github.a-g-u-p-t-a:embeddedmq:…")` | Maven Central | `MAVEN_USERNAME`, `MAVEN_PASSWORD`, `GPG_PRIVATE_KEY`, `GPG_PASSPHRASE` |
| `cargo add emq` | crates.io | `CARGO_REGISTRY_TOKEN` |

## One-time account setup

### PyPI

1. Create account at https://pypi.org  
2. Create API token (scope: entire account or project `embeddedmq`)  
3. Repo **Settings → Secrets → Actions** → `PYPI_API_TOKEN` = `pypi-…`

### Maven Central

1. Account at https://central.sonatype.com  
2. Claim namespace `io.embeddedmq` (GitHub/`io.github.a-g-u-p-t-a` is an alternative if DNS not owned)  
3. Generate user token → secrets `MAVEN_USERNAME` + `MAVEN_PASSWORD`  
4. Create a GPG key, upload public key to a keyserver, store:
   - `GPG_PRIVATE_KEY` = `gpg --export-secret-keys --armor KEYID`
   - `GPG_PASSPHRASE` = passphrase  

Guide: https://central.sonatype.org/publish/publish-portal-maven/

### crates.io

1. Account at https://crates.io  
2. API token → secret `CARGO_REGISTRY_TOKEN`  
3. Publish order: `emq-sys` then `emq` (workflow handles this)

### Go

No registry account. Nested module tag is enough:

```text
bindings/go/v1.0.0-beta.1
```

Users:

```bash
go get github.com/A-G-U-P-T-A/EmbeddedMQ/bindings/go@v1.0.0-beta.1
```

## Namespace note (Java)

The POM uses `io.github.a-g-u-p-t-a` so Sonatype can verify ownership via this
GitHub account (no custom domain required). After you own `embeddedmq.io`, you
can rename `groupId` to `io.embeddedmq` and re-claim that namespace.
