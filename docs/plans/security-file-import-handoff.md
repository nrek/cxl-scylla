---
project: cxl-scylla
date: 2026-09-10
status: implemented-validation-partial
---

# Security file import

The Fluent import button previously called save_item immediately with the form's current metadata. Selecting a file before entering a reference name failed, and the separate Save button submitted the empty password field.

SecuritySettingsPane.cs now stages the file contents in memory and shows the filename with instructions to save. Save/Replace submits the attachment. Failed saves retain the input for correction; typing a value replaces the attachment. Unloading the form clears pending input. Empty files and NUL-containing binary input are rejected, and the existing 1 MiB file limit remains. Secret contents are never displayed in the attachment status.

Validation: Roslyn syntax parsing passed. Full builds were attempted but blocked by local WinUI XAML compiler initialization failures (0x8009001D); the alternate task path also failed to instantiate. No rebuilt app was deployed and no interactive picker/vault test was performed. STRATA tools were unavailable; the local project Security status document was reviewed.

Manual acceptance after a successful build: attach a disposable PEM before entering a name; verify the filename appears without creating an item; enter a name and save. Also check replacement, picker cancellation, invalid-name save followed by correction, typing over an attachment, empty/oversize files, and closing Security with pending input. Use disposable credentials only.
