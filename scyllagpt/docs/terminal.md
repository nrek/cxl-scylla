# Terminal behavior and verification

The terminal uses ConPTY with an incremental VT screen model. Cursor movement,
line/display erasure, insert/delete, scrolling regions, alternate screens, and
UTF-8 decoding survive arbitrary pipe-read boundaries. A read-only RichEdit view
renders the screen and bounded scrollback; shell input is sent only to ConPTY.
The caret follows the shell cursor. Selection supports Ctrl+C; without a selection,
Ctrl+C interrupts the shell. Ctrl+V and Shift+Insert paste into the shell.

Closing the panel terminates its terminal sessions and their process trees.
Closing the last session dismisses the panel instead of creating a replacement.
Collapse and switching to Output/Problems temporarily hide sessions. The divider
has a reserved eight-DIP gap above the panel for dragging.

The renderer currently uses uniform text color. SGR styling, terminal mouse
reporting, and double-width/combining-character cell geometry are not implemented;
this is not a complete xterm implementation.

`scyllagpt-tests` includes screen-stream regression tests and a hidden-window test
with a real cmd.exe ConPTY session. It checks output, Backspace and arrow editing,
hidden visibility after output, prompt shutdown, child-window removal, and shell
termination. No visible application is launched by these tests.

Manual check after rebuilding/relaunching:

1. Open a terminal. Type a command, use Backspace, Home/End and arrows to edit it,
   then submit it. Check that the prompt and output remain in order.
2. Drag the horizontal divider above the terminal up and down.
3. Create two sessions, switch between them, and visit Output/Problems.
4. Close a session; close the last session. The panel must stay dismissed.
5. Open another terminal and close the entire panel. Reopening starts a fresh shell.

When developing inside Scylla, build to a separate directory so the running
executable and active conversation can remain open until verification finishes.
