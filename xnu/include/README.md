# Vendored XNU headers

These are unmodified copies from Apple's open-source XNU, used only by the
host tests that verify karnak's `boot_args` mirror:

- Source: <https://github.com/apple-oss-distributions/xnu>
- Tag: `xnu-8796.141.3` (the version `xnu/README.md` targets)
- Files:
  - `pexpert/pexpert/arm64/boot.h`
  - `pexpert/pexpert/arm/protos.h`
  - `pexpert/pexpert/arm/consistent_debug.h`

The include root is `xnu/include/pexpert`, matching XNU's own
`-I pexpert/..` layout, so `#include <pexpert/arm64/boot.h>` resolves exactly
as it does in the kernel build.

These headers are licensed under the Apple Public Source License 2.0, not the
repository's MIT/BSD licenses.  They are **not** compiled into firmware:
`platform/edk2/XnuLoaderPkg/Include/KarnakBootArgs.h` mirrors the structure with
UEFI types, and `xnu/tests/boot_args_test.c` asserts that every field offset and
the total size still match this copy.

To refresh after an XNU bump, replace the files, then run the test; the test is
the gate that catches layout drift.
