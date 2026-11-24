# rootfs

Goal: a **disk-backed** Darwin rootfs on the eMMC, not a RAM disk (the device
has 1.5 GB of RAM, most of which is needed by the kernel and SpringBoard).

Nothing here produces a bootable image yet.  The scripts document the intended
pipeline and keep the layout decisions in one place.

## Plan

1. Build or obtain an HFS+ image (`rootfs/base.hfs`) containing
   `/System/Library/CoreServices`, `/usr/lib/dyld`, and the launchd tree.
2. Shrink it to a fixed-size partition (8–12 GB) rather than a sparse image so
   the MSDC0 driver can address it linearly.
3. Flash it to the `userdata` partition; keep `system` read-only.
4. Point XNU's `rootdev` at the MSDC0 block device.

## Memory tuning for 1.5 GB

- `jetsam` memory limits must be lowered: SpringBoard on 1.5 GB needs roughly
  `JETSAM_MEMORY_LIMIT_MB` ~ 650 for the foreground app.
- Keep the kernel's `max_mem` at the runtime DTB value.
- Disable memory-hungry daemons (Spotlight, photo analysis) in the image.
- Use 4K pages for the kernel if the port allows it; 16K pages waste ~3% of a
  1.5 GB device in slack.

## Tools

`./slim_rootfs.sh <base.hfs> <out.img>` will resize and trim once a base image
exists.  On Linux it needs `hfsprogs`; on macOS it uses `hdiutil`.
