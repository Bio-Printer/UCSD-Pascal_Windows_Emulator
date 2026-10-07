# UCSD Pascal II.0 Editor — Keyboard Cheat Sheet
*UCSD Pascal II.0 Emulator v1.68 with the matching `SYSTEM.MISCINFO` (in `tools/`)*

The editor has two levels. At the **command level** the top line is the
`>Edit:` prompt and you move around and give one-letter commands. In
**Insert, Delete and eXchange modes** you type or delete text and finish with
**Ctrl-C** (keep the change) or **Esc** (throw it away).

## Moving around (command level)

| Key | Action |
|---|---|
| ↑ ↓ ← → | Move the cursor one line / one character |
| **Page Down** / **Page Up** | Forward / back one screen (44 lines) |
| **Home** / **End** | Beginning / end of the file |
| **Return** | Start of the next line |
| **Space** | One character in the current direction |
| a number, then a move | Repeat the move (e.g. `5` ↓ goes down 5 lines) |
| `=` | Back to the start of the last insertion |
| `>` `.` `+` | Set the direction **forward** (shown as the first character of the prompt) |
| `<` `,` `-` | Set the direction **backward** |
| `J` | Jump — the prompt offers **B**eginning, **E**nd, **M**arker |

## Changing text

| Key | Action |
|---|---|
| **Insert** (or `I`) | Start inserting text at the cursor |
| **Delete** | Delete the character under the cursor |
| `D` | Delete mode: move over the text to delete, then Ctrl-C (or Esc to cancel) |
| `X` | eXchange: type over existing text, then Ctrl-C |
| **Ctrl-C** | Accept — finishes Insert, Delete or eXchange and keeps the change |
| **Esc** | Cancel — leaves Insert, Delete or eXchange without changing anything |

While inserting:

| Key | Action |
|---|---|
| **Backspace** | Remove the last character typed |
| **Ctrl+Backspace** | Erase the line you are typing (on the first line of an insertion the editor answers "No insertion to back over") |
| **Return** | New line |

Page Up/Down, Home, End, Insert and Delete type the editor commands above (`>P`, `<P>`, `JB`, `JE`, `I`, `D` ^U ^C). A program that sets SYSCOM^.EXPANSION[1] to 25605 while it runs (Tiny-C's vi) gets one code each instead (version 2.00; README).

## Other editor commands (letters at the `>Edit:` prompt)

| Key | Command |
|---|---|
| `A` | Adjust — shift lines left or right |
| `C` | Copy — from the copy buffer or from a file |
| `F` | Find a string |
| `R` | Replace a string |
| `Z` | Zap — delete from the last find/replace/insert position to the cursor |
| `S` | Set markers and editing options |
| `V` | Verify — redraw the screen |
| `M` | Macro define |
| `Q` | Quit — then **U**pdate the workfile, **E**xit without saving, **R**eturn to the editor, or **W**rite to a file |

## Keys that work everywhere in the system

| Key | Action |
|---|---|
| **Ctrl-S** | Stop / restart screen output |
| **Ctrl-F** | Flush (discard) screen output |
| **Ctrl-C** | End of file when a program reads the keyboard |
| **Backspace** | Delete the last character typed in an answer |
| **Ctrl+Backspace** | Erase the whole answer being typed |

## Installing the matching `SYSTEM.MISCINFO`

The v1.68 emulator and the new `SYSTEM.MISCINFO` belong together (the right
arrow key moved from Ctrl-S to Ctrl-U, and Ctrl-C became the accept key):

1. In the **Filer**, `C`hange `SYSTEM.MISCINFO` to `OLD.MISCINFO` (keeps a backup).
2. **Options → Import Windows File to Volume**: `tools\SYSTEM.MISCINFO` to unit **#4**.
3. Restart the emulator — the system reads `SYSTEM.MISCINFO` when it starts.

To go back: remove `SYSTEM.MISCINFO` and change `OLD.MISCINFO` back.

## What the settings are (for reference)

| Setting | Value |
|---|---|
| Screen | 132 columns × 45 rows (console, `SYSTEM.MISCINFO` and `GOTOXY` all agree) |
| Arrow keys sent | up Ctrl-T, down Ctrl-R, left Ctrl-Q, right Ctrl-U |
| Page Up / Page Down | `<P>` / `>P` |
| Home / End | `JB` / `JE` |
| Insert / Delete | `I` / `D`, right, Ctrl-C |
| Editor accept / escape | Ctrl-C / Esc |
| Screen control codes | Ctrl-A x y (cursor position), Ctrl-B erase to end of line, Ctrl-C erase to end of screen, Ctrl-D up, Ctrl-E right, Ctrl-F home, Ctrl-L clear screen |
