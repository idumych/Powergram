#!/usr/bin/env python3
"""Teach TDLib about Haiku. Idempotent; run on a TDLib checkout:

    python3 tools/patch-tdlib.py <td checkout>

Haiku is POSIX with poll() and pthreads, but defines __HAIKU__ rather than
__unix__, so TDLib's platform detection stops at #error. TD_HAIKU takes the
same port choices as Solaris: poll() and the socketpair-based event fd.
"""
import sys, os

root = sys.argv[1]

def patch(rel, old, new):
    path = os.path.join(root, rel)
    text = open(path).read()
    if new in text:
        return
    if old not in text:
        sys.exit(f"patch-tdlib: anchor not found in {rel}:\n{old}")
    open(path, 'w').write(text.replace(old, new, 1))
    print(f"patched {rel}")

patch('tdutils/td/utils/port/platform.h',
      '#elif defined(__unix__)  // all unices not caught above',
      '#elif defined(__HAIKU__)\n  #define TD_HAIKU 1\n'
      '#elif defined(__unix__)  // all unices not caught above')

patch('tdutils/td/utils/port/config.h',
      '#elif TD_SOLARIS\n  #define TD_POLL_POLL 1\n  #define TD_EVENTFD_BSD 1\n',
      '#elif TD_SOLARIS\n  #define TD_POLL_POLL 1\n  #define TD_EVENTFD_BSD 1\n'
      '#elif TD_HAIKU\n  #define TD_POLL_POLL 1\n  #define TD_EVENTFD_BSD 1\n')

# sqlite's os_unix code uses mmap(), but the amalgamation includes
# <sys/mman.h> only in SQLCipher's memlock block, which checks __unix__.
patch('sqlite/sqlite/sqlite3.c',
      '#if !defined(SQLITE_OMIT_WAL) || SQLITE_MAX_MMAP_SIZE>0\n/* # include <sys/mman.h> */',
      '#if !defined(SQLITE_OMIT_WAL) || SQLITE_MAX_MMAP_SIZE>0\n/* # include <sys/mman.h> */\n'
      '#ifdef __HAIKU__\n# include <sys/mman.h>\n#endif')
