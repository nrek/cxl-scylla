# Markdown surfaces

Chat messages, Markdown file previews and STRATA document details share the native
ui_kit Markdown renderer. Supported syntax includes ATX headings, bold, italic,
strikethrough, inline backtick code, fenced code, bullet and numbered lists,
blockquotes, horizontal rules, inline links and angle-bracket HTTP(S) autolinks.
Line breaks and blank lines are preserved. Unfinished inline syntax stays literal
during streaming and is interpreted when its closing delimiter arrives.

Markdown files (.md, .markdown, .mdc, .mdx) default to Preview. Source/Preview switches
between the formatted read-only surface and the original Scintilla buffer; saves
always use the source buffer, including unsaved edits. Large-file fallback stays
in source view. Find searches the visible preview; Go to Line uses source view.

Local links open through the existing permission-aware document handler. Relative
links in file previews resolve from that file's folder; chat and STRATA links
resolve from the current project. Absolute Windows paths, file:/// paths, encoded
spaces and :line suffixes are supported. HTTP(S) links open the browser on click.
Other schemes are not launched. Raw HTML is displayed, never executed; images are
not fetched. This is a common Markdown subset, not full CommonMark/GFM: tables,
reference-style links, heading anchors, embedded images and MDX components are not
rendered as specialized elements. Fenced code is monospaced without syntax coloring.

User messages with attached files or images include a clickable “(n) Attachments
[+]” footer. It opens a dark tray below the footer with square image thumbnails or
file tiles. Selecting a tile opens a read-only in-app lightbox: images scale to fit
and readable text files show a bounded preview. Missing, binary, oversized or newly
permission-denied Knowledge files fail softly. Attachment paths and labels persist
inside the hidden JSON display envelope for restored Codex and Claude transcripts.
No attachment is executed and no remote image is fetched.

Opening or restoring a chat renders its history and anchors the transcript at the
bottom. Live responses continue following only while the user remains at the bottom.

Shared APIs: ui_kit::create_markdown_view, append_markdown, set_markdown,
markdown_link_at, trim_markdown_links. Link offsets use RichEdit character positions
and are cleared on replacement/destruction. The UI Gallery includes a sample.

Verification: build/verify-markdown.ps1 builds the app, runs the full regression
suite and runs scyllagpt-markdown-tests against hidden native RichEdit controls.

Scrollable ui_kit surfaces use the shared thin scrollbar skin. Native non-client
thumb tracking runs a short repaint timer so Windows cannot expose its stock light
scrollbar during a drag; release and cancellation restore the normal thumb state.
Windows high-contrast mode retains the system scrollbar intentionally.
The shared skin also repaints after content, font, focus, visibility and enabled
state changes. ui_kit hosts retain their dark native control theme as a fallback,
so frequently updated views such as Agent Working activity remain dark after use.
Mouse and non-client interactions schedule a deferred repaint after Windows finishes
its own processing, covering click and wheel states where stock scrollbar chrome can
otherwise overwrite the Scylla overlay.
