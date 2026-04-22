// Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
// SPDX-License-Identifier: BSD-2-Clause
//
// LiteX-port architecture-specific lwIP shims. Picolibc provides the
// usual C library so the assert/printf-flavoured macros below just
// route through it.

#ifndef MICROPY_INCLUDED_LITEX_LWIP_ARCH_CC_H
#define MICROPY_INCLUDED_LITEX_LWIP_ARCH_CC_H

#include <assert.h>

// Drop lwIP's debug prints — picolibc's stdout is the REPL UART and
// we don't want lwIP chatter mixing into it. Enable selectively via
// LWIP_DEBUG/LWIP_DEBUGF if a hard-to-find issue ever justifies it.
#define LWIP_PLATFORM_DIAG(x)
#define LWIP_PLATFORM_ASSERT(x) { assert(1); }

// Picolibc has ctype.h but lwIP's ctype shim doesn't play nicely with
// it. Tell lwIP to use its own.
#define LWIP_NO_CTYPE_H 1

#endif // MICROPY_INCLUDED_LITEX_LWIP_ARCH_CC_H
