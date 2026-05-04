#ifndef EDITOR_H
#define EDITOR_H

#include "honeyfs.h"

#define EDITOR_MAX_COLS 80
#define EDITOR_MAX_ROWS 24 // Row 25 is reserved for the status bar

// The signature must include HoneyFS* as the first argument
void editor_start(HoneyFS *fs, const char* filename);

#endif
