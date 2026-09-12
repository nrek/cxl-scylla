# Markdown image preview

Open this file in **Preview**. The local logo should appear at the pane width with its proportions preserved. Resize the editor to verify it fits.

![Scylla logo](../docs/images/scylla-logo.png "Scylla logo")

Images can appear between text: ![Another logo](<../docs/images/scylla-logo.png>) and the text continues below.

> ![Logo in a quote](../docs/images/scylla-logo.png)

![Missing fixture](missing-image.png)

The missing image above should show “Image unavailable: Missing fixture”.

These examples must remain literal:

`![Inline code](../docs/images/scylla-logo.png)`

```markdown
![Fenced code](../docs/images/scylla-logo.png)
```

An optional remote image (requires network access):

![Remote PNG](https://www.w3.org/Icons/w3c_home.png)
