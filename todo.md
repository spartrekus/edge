## Display

### Syntax

Correctly handle: '\000'

Support more languages:
  - Python

Improve support for Markdown syntax.
  Support lists.

## Editing

Implement delete of page.

Let buffers "garbage collect" their contents: if they're clean and haven't been accessed in a while, just have them drop their contents.
  - Requires making them load them lazily.
  - Probably not too important? We can just let the OS page out appropriate pages.

For 'd': Add '?' (show modifiers available).

Add "pipe" command: select the region (similar to delete: line, paragraph, buffer...), and then prompt for a command. Pipe the contents of the region to the command, and replace them with the output of the command.

Add an autocomplete mode that autocompletes based on the path.

Improve logic around wrap_long_lines.
  Cursors (other than active cursor) are off.

## Navigation

Improve "g", the current behavior is kind of annoying:
  There should be a way (other than pressing it again) to specify if it should ignore space.  Maybe a modifier can do it?

Honor the "margin_columns" variable.

Add a boolean variable "highlight_current_line" (default: false); when set, highlight the line with the current cursor position.

If the buffer doesn't fit in the screen, don't show the scroll bar (or show it in a different way).

### List of buffers

## Prompt

Improve the history functionality for commands.
  For "af": Perhaps have a directory with a file per-command, that keeps all history for that command.
    The command would be the basename of the first token given to an "af" command?
    It may back-fire with some commands (shell commands, like "for a in $(seq 0 10); do echo $a; done"), but it's probably OK.
    Then improve somehow the completion or history iteration logic?

## Commands

Improve "af":
  Add more structures to "af":
    BUFFER> run a command with the whole contents of the buffer
      (another possibility: run a command for each line in buffer (prompt))
  Switch it to use the "new" enter-applies-command (similar to "d") mode.

When running subcommands with aC, actually set variables "ARG0", "ARG1", "ARG2", "ARG3", with words in line...

Create some "barriers" that limit the number of subprocesses.  Set a limit for each.  Maybe as a special type of buffer?  Let it reload to show the state?
  Commands should by default go against a shared barrier, but should have a variable that specifies what barrier to use.

Persist undo history?

Make follow_end_of_file the default.

## Variables

atomic_lines should probably also apply to multiple cursors.

## VM

Add support for templates, so that we can do "vector<string>".

Support polymorphism: same name for multiple symbols with different type.
  Requires adjusting lookup and all callers.

Support in-line functions and/or lambda forms. Tricky.

### Client/server

Allow a client to just disconnect.
  This is currently hard because the server doesn't know which client issued the commands it processes.

## Misc

Support variables scoped at the Editor level (i.e. not specific to a given buffer).
