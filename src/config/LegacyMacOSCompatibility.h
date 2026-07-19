/*
 * Xcode 26 defines TARGET_OS_MAC before these legacy libraries are parsed.
 * Their old zlib and libpng code interprets the mere presence of that symbol
 * as classic Mac OS, so include Apple's definitions once and then hide only
 * that obsolete compatibility spelling.
 */
#pragma once

#include <TargetConditionals.h>
#undef TARGET_OS_MAC
