#pragma once

// helpers for inspecting the flags passed to open()

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// Mask off everything but the access flags, so we can do comparisons
// like OFLAG_ACCESS(flags) == O_RDONLY
#define OFLAG_ACCESS(flags) \
	((flags) & (O_RDONLY | O_RDWR | O_WRONLY))
