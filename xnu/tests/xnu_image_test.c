/*
 * Host tests for platform/edk2/XnuLoaderPkg/Library/XnuImageLib.
 *
 * Build (see .github/workflows/xnu.yml):
 *   cc -std=c11 -Wall -Wextra -Werror \
 *      -I xnu/tests/hostshim -I platform/edk2/XnuLoaderPkg/Include \
 *      xnu/tests/xnu_image_test.c \
 *      platform/edk2/XnuLoaderPkg/Library/XnuImageLib/XnuImageLib.c \
 *      -o /tmp/xnu_image_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Library/XnuImageLib.h>

static int failures;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      failures++;                                                       \
    }                                                                   \
  } while (0)

static void
Put32 (unsigned char *p, uint32_t v)
{
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16);
  p[3] = (unsigned char)(v >> 24);
}

static void
Put64 (unsigned char *p, uint64_t v)
{
  Put32 (p, (uint32_t)v);
  Put32 (p + 4, (uint32_t)(v >> 32));
}

static void
PutName (unsigned char *p, const char *name)
{
  size_t n = strlen (name);
  if (n > 15) {
    n = 15;
  }
  memcpy (p, name, n);
}

/* Append a segment_command_64; returns bytes written. */
static size_t
AddSegment (
  unsigned char *p,
  const char    *name,
  uint64_t      vmaddr,
  uint64_t      vmsize,
  uint64_t      fileoff,
  uint64_t      filesize,
  uint32_t      maxprot,
  uint32_t      initprot
  )
{
  memset (p, 0, 72);
  Put32 (p, XNU_LC_SEGMENT_64);
  Put32 (p + 4, 72);
  PutName (p + 8, name);
  Put64 (p + 24, vmaddr);
  Put64 (p + 32, vmsize);
  Put64 (p + 40, fileoff);
  Put64 (p + 48, filesize);
  Put32 (p + 56, maxprot);
  Put32 (p + 60, initprot);
  Put32 (p + 64, 0);          /* nsects */
  Put32 (p + 68, 0);          /* flags */
  return 72;
}

/*
 * Build a small but well-formed image:
 *   __TEXT at 0xfffffff007004000, 0x4000 bytes, first 0x1000 in file
 *   __DATA at 0xfffffff007008000, 0x2000 bytes, 0x1000 in file at 0x1000
 *   __LINKEDIT vm 0x...0a000, 0x1000, file 0x2000
 *   LC_MAIN entryoff 0x200
 */
static size_t
BuildImage (unsigned char *buf, size_t cap)
{
  size_t off;

  memset (buf, 0, cap);
  if (cap < 0x3000) {
    return 0;
  }

  Put32 (buf + 0, XNU_MH_MAGIC_64);
  Put32 (buf + 4, XNU_CPU_TYPE_ARM64);
  Put32 (buf + 8, XNU_CPU_SUBTYPE_ARM64_ALL);
  Put32 (buf + 12, XNU_MH_EXECUTE);

  off = 32;
  off += AddSegment (buf + off, "__TEXT", 0xFFFFFFF007004000ULL, 0x4000,
                     0, 0x1000, 5, 5);
  off += AddSegment (buf + off, "__DATA", 0xFFFFFFF007008000ULL, 0x2000,
                     0x1000, 0x1000, 3, 3);
  off += AddSegment (buf + off, "__LINKEDIT", 0xFFFFFFF00700A000ULL, 0x1000,
                     0x2000, 0x1000, 1, 1);

  /* LC_MAIN */
  Put32 (buf + off, XNU_LC_MAIN);
  Put32 (buf + off + 4, 24);
  Put64 (buf + off + 8, 0x200);
  Put64 (buf + off + 16, 0);
  off += 24;

  Put32 (buf + 16, 4);                    /* ncmds */
  Put32 (buf + 20, (uint32_t)(off - 32)); /* sizeofcmds */

  /* The file is as large as the last segment payload, even though the load
     commands themselves live in the first 272 bytes. */
  return 0x3000;
}

static void
TestValid (void)
{
  unsigned char       buf[0x4000];
  XNU_IMAGE_INFO      info;
  const CHAR8        *overlap = NULL;
  size_t              size = BuildImage (buf, sizeof (buf));

  CHECK (size > 0);
  CHECK (XnuImageParse (buf, size, &info) == EFI_SUCCESS);
  CHECK (info.SegmentCount == 3);
  CHECK (info.CmdCount == 4);
  CHECK (info.HasEntry);
  CHECK (info.EntryPoint == 0xFFFFFFF007004200ULL);
  CHECK (strcmp (info.Segment[0].Name, "__TEXT") == 0);
  CHECK (info.Segment[1].VmAddr == 0xFFFFFFF007008000ULL);
  CHECK (info.ImageBase == 0xFFFFFFF007004000ULL);
  CHECK (info.ImageEnd == 0xFFFFFFF00700C000ULL);
  CHECK (info.FileBytes == 0x3000);
  CHECK (info.VmBytes == 0x7000);

  /* Place __TEXT at 0x40000000: the DTB window at 0x4fe00000 must be clear,
     but a reservation at 0x40008000 hits __DATA. */
  CHECK (XnuImageCheckPlacement (&info, 0x40000000ULL,
                                 0x4FE00000ULL, 0x100000, &overlap) == EFI_SUCCESS);
  CHECK (XnuImageCheckPlacement (&info, 0x40000000ULL,
                                 0x40004000ULL, 0x1000, &overlap) ==
         EFI_ACCESS_DENIED);
  CHECK (overlap != NULL && strcmp (overlap, "__DATA") == 0);
  /* Empty ranges never collide. */
  CHECK (XnuImageCheckPlacement (&info, 0x40000000ULL, 0x40000000ULL, 0, NULL) ==
         EFI_SUCCESS);
}

static void
TestBadMagic (void)
{
  unsigned char  buf[0x4000];
  XNU_IMAGE_INFO info;
  size_t         size = BuildImage (buf, sizeof (buf));

  Put32 (buf, 0xCAFEBABE);
  CHECK (XnuImageParse (buf, size, &info) == EFI_INVALID_PARAMETER);
}

static void
TestWrongCpu (void)
{
  unsigned char  buf[0x4000];
  XNU_IMAGE_INFO info;
  size_t         size = BuildImage (buf, sizeof (buf));

  Put32 (buf + 4, 0x01000007);            /* x86_64 */
  CHECK (XnuImageParse (buf, size, &info) == EFI_INVALID_PARAMETER);
}

static void
TestTruncatedHeader (void)
{
  unsigned char  buf[0x4000];
  XNU_IMAGE_INFO info;

  memset (buf, 0, sizeof (buf));
  Put32 (buf, XNU_MH_MAGIC_64);
  CHECK (XnuImageParse (buf, 8, &info) == EFI_VOLUME_CORRUPTED);
}

static void
TestSegmentPastEndOfFile (void)
{
  unsigned char  buf[0x4000];
  XNU_IMAGE_INFO info;

  (void)BuildImage (buf, sizeof (buf));
  Put64 (buf + 32 + 40, 0x2000);          /* __TEXT fileoff */
  Put64 (buf + 32 + 48, 0x2000);          /* filesize runs off the end */
  CHECK (XnuImageParse (buf, 0x2000, &info) == EFI_VOLUME_CORRUPTED);
}

static void
TestUnsupportedCommand (void)
{
  unsigned char  buf[0x4000];
  XNU_IMAGE_INFO info;
  size_t         size = BuildImage (buf, sizeof (buf));

  /* Rewrite the LC_MAIN slot as an unknown load command. */
  Put32 (buf + 32 + 72 + 72 + 72, 0x1234);
  CHECK (XnuImageParse (buf, size, &info) == EFI_UNSUPPORTED);
}

static void
TestMisalignedCommandSize (void)
{
  unsigned char  buf[0x4000];
  XNU_IMAGE_INFO info;
  size_t         size = BuildImage (buf, sizeof (buf));

  Put32 (buf + 32 + 4, 70);               /* cmdsize not 8-aligned */
  CHECK (XnuImageParse (buf, size, &info) == EFI_VOLUME_CORRUPTED);
}

static void
TestCorruptCounts (void)
{
  unsigned char  buf[0x4000];
  XNU_IMAGE_INFO info;
  size_t         size = BuildImage (buf, sizeof (buf));

  Put32 (buf + 20, 0x10000);              /* sizeofcmds beyond buffer */
  CHECK (XnuImageParse (buf, size, &info) == EFI_VOLUME_CORRUPTED);

  size = BuildImage (buf, sizeof (buf));
  Put32 (buf + 16, XNU_IMAGE_MAX_CMDS + 1);
  CHECK (XnuImageParse (buf, size, &info) == EFI_UNSUPPORTED);
}

static void
TestBssAndRounding (void)
{
  unsigned char  buf[0x4000];
  XNU_IMAGE_INFO info;
  size_t         size;

  size = BuildImage (buf, sizeof (buf));
  /* Give __DATA a bss tail: filesize 0, vmsize 0x2000. */
  Put64 (buf + 32 + 72 + 48, 0);
  CHECK (XnuImageParse (buf, size, &info) == EFI_SUCCESS);
  CHECK (info.Segment[1].FileSize == 0);
  CHECK (info.ImageEnd == 0xFFFFFFF00700C000ULL);
}

/* Append an LC_UNIXTHREAD load command carrying an ARM64 thread state. */
static size_t
AddUnixThread (
  unsigned char *p,
  uint64_t       pc
  )
{
  memset (p, 0, 288);
  Put32 (p, XNU_LC_UNIXTHREAD);
  Put32 (p + 4, 288);
  Put32 (p + 8, 6);            /* ARM_THREAD_STATE64 */
  Put32 (p + 12, 68);          /* count in 32-bit words */
  Put64 (p + 16 + 32 * 8, pc);
  return 288;
}

static void
TestUnixThreadEntry (void)
{
  unsigned char  buf[0x4000];
  XNU_IMAGE_INFO info;
  size_t         off;

  (void)BuildImage (buf, sizeof (buf));
  off = 32 + 72 + 72 + 72;     /* where BuildImage put LC_MAIN */
  (void)AddUnixThread (buf + off, 0xFFFFFFF007004100ULL);
  Put32 (buf + 16, 4);
  Put32 (buf + 20, (uint32_t)(off + 288 - 32));

  CHECK (XnuImageParse (buf, sizeof (buf), &info) == EFI_SUCCESS);
  CHECK (info.HasEntry);
  CHECK (info.EntryPoint == 0xFFFFFFF007004100ULL);

  /* A non-ARM64 thread state is rejected, not silently ignored. */
  Put32 (buf + off + 8, 4);    /* x86_THREAD_STATE64 */
  CHECK (XnuImageParse (buf, sizeof (buf), &info) == EFI_UNSUPPORTED);

  /* A truncated state must not be read past the command. */
  Put32 (buf + off + 8, 6);
  Put32 (buf + off + 12, 68);
  Put32 (buf + off + 4, 16 + 32 * 8);
  CHECK (XnuImageParse (buf, sizeof (buf), &info) == EFI_VOLUME_CORRUPTED);
}

int
main (void)
{
  TestValid ();
  TestBadMagic ();
  TestWrongCpu ();
  TestTruncatedHeader ();
  TestSegmentPastEndOfFile ();
  TestUnsupportedCommand ();
  TestMisalignedCommandSize ();
  TestCorruptCounts ();
  TestBssAndRounding ();
  TestUnixThreadEntry ();

  if (failures) {
    printf ("%d test(s) failed\n", failures);
    return 1;
  }
  printf ("xnu_image_test: all tests passed\n");
  return 0;
}
