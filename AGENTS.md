# AGENTS.md

## Project instruction

Project name: `codex_on_CO2_WIFI_project`

This file gives Codex project-level guidance. Before analyzing, editing, or generating code, read and follow this file.

## Working-directory alignment

This repository is the active CO2_WIFI project:

```text
/Users/kevinwei/Dropbox/0DownLoad/codex_on_CO2_WIFI_project
```

For every CO2_WIFI-project shell command:

1. Set the command `workdir` to the CO2_WIFI project path when the tool supports it.
2. Verify the working directory with `pwd` when starting or resuming work.
3. Keep source-code paths relative to the repository after the shell is aligned.
4. Do not edit, generate, or test files in other project folders unless the user explicitly requests it.
5. If the task's inherited default `pwd` points elsewhere, treat it as untrusted and realign the command explicitly.

## Title naming rule

If a title is like `title_example V07 2026.06.24 17:32`, increment the version to `V08` and change the date/time to the current local time.

## Code completion handoff rule

After generating or modifying project files:

1. Run an appropriate syntax or structural check.
2. Choose the HTML that corresponds to the completed work:
   - For QA report changes, use `docs/qa_co2_progress_daily_digest.html`.
   - For application/code changes, use `index.html`.
3. Automatically open or reload that corresponding HTML for visual checking.
4. For application/code changes, read the actual version from the completed `index.html`; never hard-code or guess it.
5. Finish the user-facing handoff with two color-coded status lines:
   - Green `🟢 JB_DONE>` reports the verified completed result.
   - Blue `🔵 JB_NEXT>` reports the single most useful next action. If no action remains, use `No pending task`.

QA report change:

```text
🟢 JB_DONE> QA Report 已更新, Checking on 'qa_co2_progress_daily_digest.html'
🔵 JB_NEXT> <next verification or action>
```

Application/code change (substitute the actual version):

```text
🟢 JB_DONE> 已修正 V1.14.0, Checking on 'index.html'
🔵 JB_NEXT> <next verification or action>
```

Keep these as the final two lines, in DONE then NEXT order. If the corresponding HTML cannot be opened automatically, state the reason immediately before the required handoff lines. Do not claim that visual or physical BLE verification passed unless it was actually performed.

## Auto QA progress record rule

Use `docs/qa_co2_progress_daily_digest.html` as the project's Question -> Answer progress record. Write future entries in Traditional Chinese, use stable three-digit indices, show `(NNN YYYY-MM-DD HH:mm)`, keep latest entries first, and keep details collapsed by default.

After completing a meaningful investigation, implementation, test, or engineering decision, add one concise QA entry with the verified answer and evidence or next verification step. Do not record assumptions as answers.

### Version-to-QA requirement

Every firmware or dashboard version update must include a corresponding new entry in `docs/qa_co2_progress_daily_digest.html` in the same work session. Record the exact version, change summary, verification evidence, and any remaining hardware validation step before handoff.

## User abbreviation rules

| Abbrev | Meaning |
|---|---|
| `go` | Read `AGENTS.md`, then perform the requested work. |
| `di` | Discuss only; do not edit or implement until the user says `go` or explicitly requests changes. |
| `autorun` | Implement, run suitable checks, and open the completed HTML in Chrome, except for physical man-in-loop actions. |
| `xx` | Put Codex on the left and this CO2_WIFI project folder on the right for checking. |
| `tc` | Reply in Traditional Chinese. |
| `en` | Reply in English. |
| `dd` | Give a detailed deep-dive explanation. |
| `gg` | Generate a useful diagram, call tree, flow chart, or visual explanation. |
| `nu` | Explain the principle numerically, step by step. |
| `ss` | Significantly summarize the supplied source. |
| `ee` | Improve the user's English and provide both English and Chinese. |
| `li` | Run the local index flow and report the full title ending in `completed`. |
| `gi` | Run the global publish/index flow and report the full title ending in `completed`. |

## Coding and debug style

- Prefer practical, minimal, easy-to-modify code and small patches.
- Preserve existing function names and data flow where possible.
- Prefer relative, cross-platform paths over hard-coded paths.
- Report bugs in this order: likely root cause, evidence, minimal fix, safer long-term fix, test method.
- Do not claim BLE hardware behavior is verified without a real received packet.
- For plans, use numbered steps and a concise status/check table.
- Be direct and engineering-focused; use Traditional Chinese when the user writes `tc`.
