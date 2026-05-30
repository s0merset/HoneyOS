#ifndef EDITOR_H
#define EDITOR_H

/*
 * editor.h — HoneyEdit Text Editor API
 * CMSC 125 — Operating Systems, UP Cebu
 *
 * HoneyEdit is HoneyOS's built-in full-screen text editor.
 * It is launched from the File Manager Shell via the "edit <file>" command.
 *
 * The editor operates on an 80×24 character buffer (the 25th row is
 * reserved for the status bar showing filename and key hints).
 * Navigation uses arrow keys; ESC saves and exits back to the shell.
 *
 * The editor requires an already-existing file — use "create <file>"
 * first if the file does not exist yet.
 */

#include "honeyfs.h"

/* ── Editor Buffer Dimensions ── */
#define EDITOR_MAX_COLS 80    /* One full VGA text row (columns 0-79) */
#define EDITOR_MAX_ROWS 24    /* Row 25 is reserved for the status bar */

/*
 * editor_start — Launch the full-screen text editor for a given file.
 *
 *   fs       : Pointer to the mounted HoneyFS context (used to read/write the file)
 *   filename : Name of the file to open (must already exist on disk)
 *
 * Behavior:
 *   - Loads the file's current content into an 80×24 text buffer
 *   - Renders the buffer to VGA memory and enters the edit loop
 *   - Arrow keys move the cursor; Backspace erases; Enter adds a newline
 *   - Pressing ESC saves the buffer back to the file and returns to the shell
 *   - Trailing empty rows and spaces are trimmed before saving
 *
 * Note: The signature must include HoneyFS* as the first argument
 *       so the editor can call fs_write() to persist changes.
 */
void editor_start(HoneyFS *fs, const char* filename);

#endif
