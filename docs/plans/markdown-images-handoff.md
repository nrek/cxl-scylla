---
project: cxl-scylla
date: 2026-09-10
status: implemented
---

# Images in Markdown Preview

The editor now passes the current Markdown file's directory through MermaidPreview to MarkdownView. Preview recognizes inline image syntax, renders images as separate visual blocks in prose/quotes/table cells, and retains the surrounding text. Chat rendering is unchanged.

Supported sources: relative paths, absolute local paths, file URIs, and HTTP(S) URLs. Angle-bracket destinations support spaces; percent-encoded paths, escaped punctuation, balanced parentheses, alt text and optional quoted titles are handled. Code spans and fenced code remain literal. Native BitmapImage/SvgImageSource load images asynchronously, fit them to the available width with a 640-DIP height limit, and preserve proportions. Raster decoding is capped at 1600 pixels wide. Alt text supplies the accessible name and unavailable-image fallback; titles supply tooltips. Existing link resolution rejects unsupported URI schemes.

Scope limits: this implements inline `![alt](destination)` syntax, not reference-style images, HTML img tags, or image-as-link wrappers. Codec-dependent formats follow Windows support. Remote images require network access. No changes were made to native Markdown parsing or chat rendering.

Review MarkdownImage.cs (syntax), MarkdownImageView.cs (loading/layout), MarkdownView.cs (composition), and the EditorPane.cs/MermaidPreview.cs directory plumbing. tests/markdown-images.md is the visual fixture.

Validation: Fluent Release build succeeded with zero warnings/errors. The fluent-preview-tests runner passed existing encoding/link cases plus image parsing and path-resolution cases (multiple images, nested/escaped parentheses, spaces, titles, code spans, missing files and unsupported schemes). UI rendering, native image decoder output and live remote loading were not manually exercised. Build output: .tmpcl/markdown-image-build. No running app was replaced or restarted.

Project context: reviewed the latest verbose-agent-progress handoff. STRATA tools are unavailable in this session; no unfiltered workspace query was issued.
