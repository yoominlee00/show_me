# show-me eBPF Agent

Linux-only C/libbpf CO-RE Agent. Its first functional release will collect TCP connection lifecycle and retransmission metadata, then send normalized events to the central ingestion API.

## Status

The current implementation attaches `sock:inet_sock_set_state` and the BTF tracepoint `tp_btf/tcp_retransmit_skb`. It prints TCP `established`, `closed`, and correlated `retransmit` events with process and socket metadata. It does **not** send events to the ingestion API yet; HTTP transport, batching and retry are a separate next step.

## Supported lab

- Ubuntu 22.04 or 24.04
- BTF enabled at `/sys/kernel/btf/vmlinux`
- x86_64 or arm64 Linux
- `clang`, `llvm`, `make`, `bpftool`, `libbpf-dev`, `libelf-dev`, `zlib1g-dev`, `libcurl4-openssl-dev`

macOS and Docker Desktop are not supported validation environments because the Agent attaches to the host Linux kernel.

## Build

```bash
sudo apt-get update
sudo apt-get install -y clang llvm make libbpf-dev libelf-dev zlib1g-dev \
  libcurl4-openssl-dev linux-tools-common "linux-tools-$(uname -r)"

BPFTOOL="$(command -v bpftool || find /usr/lib/linux-tools/$(uname -r) -name bpftool -type f | head -n 1)" \
  make -C agent
```

`make` generates `bpf/vmlinux.h` from the current host’s BTF and creates `build/show-me-agent`. Generated files are ignored by Git.

## Run

Use a disposable Linux VM first. Runtime privilege requirements depend on the kernel; modern systems generally need `CAP_BPF` and `CAP_PERFMON`, while older kernels may require `CAP_SYS_ADMIN`. `INGEST_API_TOKEN` is required; `SHOW_ME_INGEST_URL` defaults to `http://localhost:8080/api/v1/events`.

```bash
sudo INGEST_API_TOKEN=local-dev-token ./agent/build/show-me-agent
```

Stop with `Ctrl+C`; libbpf detaches the loaded programs during process cleanup.

The complete event contract and step-by-step validation plan are in [../docs/ebpf-agent-plan.md](../docs/ebpf-agent-plan.md).
