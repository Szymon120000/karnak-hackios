# rootfs

Goal: a **disk-backed** Darwin rootfs on the eMMC, not a RAM disk (the device
has 1.5 GB of RAM, most of which is needed by the kernel and SpringBoard).

The filesystem contents still have to come from an IPSW-derived Darwin bundle.
What exists here is the **container**: a GPT disk image with a FAT boot
partition and an HFS+ root partition, generated deterministically and checked
in CI.

## Layout

| # | Name | Type GUID | Size | Purpose |
|---|------|-----------|------|---------|
| 1 | `KARNAK-BOOT` | EFI System (`EBD0A0A2-…`) | 16 MiB | kerncache, boot manifest, recovery notes |
| 2 | `KARNAK-ROOT` | Apple HFS+ (`48465300-…`) | rest | Darwin root filesystem |

Both partitions carry a karnak-specific *partition* GUID
(`6B61726E-…-000000000001/2`) so `XnuLoader` and the MSDC0 driver can find them
by name even if the disk is cloned.  The GPT is 512-byte-sector, 1 MiB aligned,
with primary and backup headers and matching CRC32s.

## Tools

- `mkrootfs.py` — assemble a GPT image around an existing HFS+ filesystem.
  Dependency-free (stdlib only); `--inspect` validates an image.

  ```sh
  python3 rootfs/mkrootfs.py --hfs base.hfs -o out/rootfs.img -s 8G
  python3 rootfs/mkrootfs.py --inspect out/rootfs.img
  ```

- `slim_rootfs.sh <base.hfs> <out.img> [size]` — grow the HFS+ payload with
  `hdiutil` (macOS) or `resize_hfs` (Linux), then wrap it with `mkrootfs.py`.

- `test_mkrootfs.py` — host tests (no external tools).  CI also runs
  `sgdisk --verify` on a generated image.

## Pipeline

1. Build or obtain an HFS+ image (`base.hfs`) containing
   `/System/Library/CoreServices`, `/usr/lib/dyld`, and the launchd tree.
2. Grow it to the target partition size; a fixed-size image is required so the
   MSDC0 driver can address it linearly.
3. Wrap it with `mkrootfs.py` (or `slim_rootfs.sh`).
4. Flash to the eMMC `userdata` area; keep the `system` area read-only.
5. Point XNU's `rootdev` at the `KARNAK-ROOT` partition.

## Memory tuning for 1.5 GB

- `jetsam` memory limits must be lowered: SpringBoard on 1.5 GB needs roughly
  `JETSAM_MEMORY_LIMIT_MB` ~ 650 for the foreground app.
- Keep the kernel's `max_mem` at the runtime DTB value (the static DTS says
  512 MiB; the live DTB carries the product value — see `docs/HARDWARE.md`).
- Disable memory-hungry daemons (Spotlight, photo analysis) in the image.
- Use 4K pages for the kernel if the port allows it; 16K pages waste ~3% of a
  1.5 GB device in slack.
