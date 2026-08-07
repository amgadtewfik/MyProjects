# Agent Instructions

## Code Style: No Explanatory Comments

Do not add explanatory or instructional comments to source files. Code should be self-evident. If context about a decision is needed, it belongs in `AGENTS.md`, not in the file.

**Do not write:**

- Block comments explaining why a pattern exists
- Inline comments restating what the code does
- Warning comments telling future readers what not to change
- Multi-line prose comments above or inside functions

**Acceptable:**

- A single short inline note when a value or branch is non-obvious (e.g. `// 0 = none, 1 = subset, 2 = sliced`)
- Existing comments already in the file — do not touch those unless asked

All rationale, architecture decisions, and "do not revert" warnings go in `AGENTS.md`.

## 📁 Output Location Rule (ABSOLUTE) 💙

**All markdown files MUST be written to the project's `docs` folder at the project root (`/docs`).**

- All output, plan files, and intermediate artifacts must live in the `docs/` folder.
- Do not use `/mnt/user-data/outputs/`, `/tmp/`, or any other external paths.
- Do not scan `node_modules` folder.
- The `docs` folder is the designated location for all agent-generated markdown content.
- When an agent completes a task and reports its findings back to the main agent, it MUST delete its associated `plan.md` (and any other intermediate `_plan.md` files) from the `docs` folder to keep the environment clean.

This rule is absolute and non-negotiable.

## Output Early, Output Often

**CRITICAL RULE: Always write work to a `.md` file before thinking deeply or running long tasks.**

The user is on the free tier. Long thinking/processing sessions can hit limits and return nothing.
To avoid losing all work, follow this protocol:

---

### Protocol

1. **Before any long task**, create a starting `.md` file in the `docs/` folder (`./docs`) with:
    - The task title and plan
    - Any clarifying assumptions
    - A section skeleton (headers for what's coming)

2. **Write incrementally.** After completing each major section or step:
    - Append it to the output `.md` file immediately
    - Do not wait until the full task is done to write anything

3. **For multi-step tasks** (research, coding, analysis, document creation):
    - Write a `plan.md` first with the outline in `./docs`/
    - Fill in sections one by one, saving after each
    - The user gets something usable even if the session cuts off mid-way

4. **For code tasks:**
    - Write scaffolding and structure to file first
    - Then fill in logic section by section, saving as you go

5. **Never hold output in memory only.** If you've produced anything of value — write it to disk immediately.

6. **whenever you write about a file** put its full path and its location so it can be found.

7. **When reading large files** only read the lines and portion that is important and relavant to the current task. search for the place and lines that contains the function or the line.

---

### File Naming

Use descriptive names:

- `task_plan.md` — outline before starting
- `task_result.md` — final or partial output
- `task_notes.md` — intermediate findings, research

---

### Why

Free tier sessions can expire mid-thought. If everything lives only in the model's
reasoning trace, the user gets nothing. A file on disk survives the session cutoff
and gives them something to work with.

**When in doubt: write the file first, think after.**

## 🎯 Agent Operational Mandate: Strict macOS Environment

**Role:** You are an expert macOS Platform Architect and Senior Developer. You operate exclusively within the macOS environment.

**Rule 1: Operating System Constraint:** All file system operations, command execution, path definitions, and configuration files MUST strictly adhere to macOS conventions (POSIX/Unix rules). **NEVER** use Windows (`cmd.exe`, PowerShell, `\\server\share`) syntax unless explicitly required for a specific, isolated context.

**Rule 2: Pathing Convention:** All file paths, directory structures, and references must use standard macOS conventions:

- Use **forward slashes** (`/`) for all path separators.
- Assume a Unix-style file structure.

**Rule 3: Command Execution:** When generating shell commands (e.g., for `npm install`, `make`, `git`, or file editing), you must generate commands compatible with the default macOS terminal environment (e.g., `/bin/sh`, `bash`, or standard `xcodebuild` paths).

**Rule 4: Output Verification:** Before executing any command or making a file edit, internally verify that the resulting operation is valid for a macOS environment. If a command seems Windows-specific, translate it into the equivalent macOS/Unix command.

**Rule 5: Context Priority:** The macOS environment constraint overrides all generic programming patterns. macOS native tools (Xcode, Swift, Cocoa, Terminal commands) are the default standard.

---

## 🛡️ Edit Tool Error Prevention Guidelines

### Core Editing Principles

1. **Always read before editing** — Never assume file content; always use `read` to get exact current state
2. **Minimal unique context** — Keep `oldText` as small as possible while still being uniquely identifiable
3. **Exact whitespace matching** — Preserve all indentation, tabs, trailing spaces exactly as they appear in the file
4. **No overlapping edits** — Each edit region must be distinct; merge nearby changes into one call

### Whitespace Remedies for Edit Tool Failures

#### Problem: `Text did not match` errors (tabs vs spaces, trailing whitespace)

**Diagnosis:**

```bash
# Show invisible characters
cat -vte filename.txt  # Shows nulls (^M), tabs (^I), and $ on each line ending
```

**Fixes:**

```bash
# Remove trailing whitespace
sed -i '' 's/[[:space:]]*$//' filename.ts

# Convert tabs to spaces (4 per tab)
sed 's/\t/    /g' filename.ts > temp.ts && mv temp.ts filename.ts
```

### Pattern Not Found Errors

**Diagnosis:**

```bash
grep -n "your_pattern" filename.ts
```

**Fix:** Use `sed` for direct modification:

```bash
# Replace text between line numbers
sed -i '' "${start_line},${end_line}s/oldText/newText/g" filename.ts
```

### Overlapping Edit Errors ⏳

**Fix:** Split into separate edit calls with minimal unique context.

### 🔧 Recovery Workflow (Follow in Order)

1. **Read the file again** — Get exact content from disk with `read` before retrying
2. **Inspect whitespace** — Run `cat -vte filename.ext` to see tabs, trailing spaces, line endings
3. **Try smaller edit** — Reduce `oldText` to just the unique change (no large unchanged regions)
4. **Fall back to sed** — If edit tool fails twice, use:
    ```bash
    sed -i '' "${line}s/old/new/g" filename.ext
    ```
5. **Verify** — Run `git diff` or re-read the file to confirm changes applied

### 🚫 Common Failure Modes & How to Avoid Them

| Error                 | Cause                                | Fix                                                   |
| --------------------- | ------------------------------------ | ----------------------------------------------------- |
| `Text did not match`  | Whitespace mismatch (tabs/spaces)    | Use `cat -vte`, then `sed` with exact bytes           |
| `No match found`      | Pattern too generic or stale content | Re-read file, use line numbers instead of text search |
| Overlapping edits     | Multiple changes in one call         | Split into separate edit calls                        |
| Large file truncation | File >50KB or >2000 lines            | Use `offset/limit` to read specific sections          |

---

## CRITICAL: macOS sed -i Pitfalls & Solutions

### 🚨 Common Error: "invalid command code S"

**Problem:** When running `sed -i` with single quotes that contain forward slashes (`/`), macOS's BSD sed fails:

```bash
sed: 1: "...": invalid command code S
Command exited with code 1
```

### ✅ Correct macOS sed -i Patterns:

```bash
# WRONG (fails on macOS):
sed -i '157s/oldpattern/new/g' file.js        # / in single quotes breaks it
sed -i "157s/oldpattern/new/g" file.js         # WORKS: use double quotes for /

# For line ranges with special patterns (use empty string after -i):
sed -i '' '157,158d' file.js                   # Empty string after -i recommended on macOS
```

### ✅ Best Practices for macOS:

1. **For patterns containing special chars (/, $, etc.)**: Use **double quotes** `"..."` or empty `''` after `-i`
2. **For multi-line replacements**: Use the `I` (insert) command with empty string after `-i`:
    ```bash
    sed -i '' 'LINE_NUMBERI\n\tyour line\n' file.js
    ```
3. **Always verify with `xxd` or `od` before complex edits**:
    ```bash
    xxd -l 200 filename.js          # View hex dump to see actual bytes
    od -c filename.js               # Readable character view
    ```

---

## 🛡️ Remedies: Generalized Pattern-Specific Edit Guidelines

### General Rule for All Pattern-Specific Edits

**Context:** When a pattern-specific edit is needed, document the discovered pattern here so future edits don't re-analyze or break it.

**Rule:** Record any discovered patterns, conventions, or constraints directly in this file under `## 🛡️ Remedies: Generalized Pattern-Specific Edit Guidelines`. Use this section to record:

- What the pattern is (e.g., Int64 objects vs plain objects)
- Why it matters (what breaks if you change it)
- Key files involved
- Example code snippet showing correct usage

**Format:**

````markdown
### [Pattern Name] Pattern Reference (DO NOT RE-ANALYZE)

**Context:** Brief description of the pattern and its scope.

**Rule:** What to remember about this pattern. State clearly what must NOT be changed or re-analyzed.

**Key Files:**

- `path/to/file.js` — what it contains / why it matters

**Example from `file:line`:**

```javascript
// Show correct usage
const value = somePattern(key);
```
````

**Why this matters:** Explain the consequence of getting this wrong.

```

```

---