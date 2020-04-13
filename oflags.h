#pragma once

// helpers for inspecting the flags passed to open()

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define OFLAG_ACCESS(flags) \
	((flags) & (O_RDONLY | O_RDWR | O_WRONLY))
