# Codex runtime discovery

| Field | Value |
|-------|--------|
| Origin | Installed by ChatGPT desktop (`OpenAI.Codex` / `OpenAI.ChatGPT-Desktop` AppX), not downloaded by this repo |
| Preferred exe | Newest versioned executable under `%LOCALAPPDATA%\OpenAI\Codex\bin\` |
| Protocol baseline | Schemas under `../schemas` |
| Architecture | x64 |
| Redistribution | Codex is not bundled. Discovery locates it; Settings can override. |

Scylla prefers a newer versioned installation over an older generic `bin\codex.exe` fallback. It upgrades a saved runtime path when discovery finds a newer product version.
