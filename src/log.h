#pragma once

// Appends a line to reVC-fpv.log (in the game's Resources folder) and echoes
// it to stderr. Thread-safe: libfpv logs from its own threads too.
void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
