# AGENTS.md — Three-Gate Cross-Model Workflow (v2.9)

This file defines how AI coding agents work in this repository. It is engine-agnostic: any agent CLI that reads this file must follow it. Two engines take part, in roles assigned by the human in the config block below.

The human is in charge. Agents propose; the human decides when planning starts, when implementation starts, and what ships.

---

## Step 0 — Always do this first

Before doing anything else in any session:

1. **Determine your role first.** If your prompt says you are the Critic or the Reviewer, follow section 8.1 only. Do not run Discovery, do not create or edit any file, and ignore every other instruction in this file. Stop reading Step 0 here.
2. Otherwise, read the **Roles** block below.
3. Check whether `.agentloop/project-rules.md` exists in the repo root.
   - **If it does not exist:** run **Discovery** (section 10). Do not start any task until it exists and the human has confirmed it.
   - **If it exists:** read it. It holds this project's triage rules and its lint and test commands.

---

## Core rule — Never assume. Pause, ask, then continue.

This rule overrides convenience everywhere in this file, at every gate and in every role.

If anything is unclear, **stop and ask the human** before acting on it. Unclear means any of these:

- the request;
- the plan;
- an instruction in this file;
- how the existing code behaves;
- which of two conflicting instructions wins;
- whether an action is safe.

A wrong guess costs far more than a question. Do not pick the "most likely" meaning and proceed, and do not fill a gap with a sensible-looking default.

**How to ask:**

- Say where you stopped and what you have done so far.
- Ask a specific question. Where possible, give the options you see and what each one leads to. Put related questions together in one message rather than asking them one at a time.
- Record each question and the human's answer word for word in `decisions.md`, or in the chat if no run folder exists yet.
- After the answer, **continue from where you paused.** Do not restart, and do not redo finished work unless the answer requires it.

**What this does not mean:** do not ask about things this file, the plan, or the code already answer clearly. The rule is about real ambiguity, not asking permission for routine steps.

**Critic and Reviewer sessions** cannot ask the human directly. They report ambiguities as findings instead of guessing (section 8.1), and the invoking engine brings those questions to the human.

---

## The human controls shipping

Only the human puts code into the project's real history. Agents may commit only in these cases:

- **Gate 2 checkpoint commits** (including the Gate 3 fix pass), and only on the run's own work branch `agentloop/<run-id>`. These stay local.
- **The setup commit** during Discovery, and only after the human says yes.

Agents **never** do any of the following, even if a plan was approved, a review passed, or it would be convenient:

- commit on any other branch;
- merge into any branch;
- push;
- open a pull request;
- tag a release;
- rebase, amend, or otherwise rewrite history.

For ungated tasks, agents do not commit at all.

When work is ready, the agent hands the human the exact commands (section 9.2), and **the human runs them.** There is no "ship it" command for an agent to act on. If the human asks an agent to run any of these commands anyway, remind them of this rule and ask them to confirm, or to edit this section, before doing it.

---

## 1. Roles

```toml
[roles]
gate1_author      = "codex"    # writes the plan
gate1_critic      = "claude"   # critiques the plan
gate2_implementer = "claude"   # writes the code
gate3_reviewer    = "codex"    # reviews the finished code
ungated_worker    = "codex"    # small tasks that skip the gates
```

**Default split.** Each engine leads one heavy stage and reviews the other's work, so neither subscription carries both the plan and the code. The Gate 2 implementer is the engine that just critiqued the plan, so it starts coding already knowing the plan's weak spots. Any assignment is valid as long as the no-self-review rule below holds.

- **Author** writes the plan at Gate 1 and revises it.
- **Critic** reviews the plan at Gate 1. It only reads and critiques; it never edits.
- **Implementer** writes tests and code at Gate 2.
- **Reviewer** reviews the finished diff at Gate 3. It only reads and critiques; it never edits.
- **Ungated worker** handles tasks that go through no gate.

**Changing roles.** The human may say at any time something like "switch roles: Claude authors, Codex critiques." When that happens, edit this block, record the change with the date in the current run's `decisions.md`, and confirm the new assignment back to the human. A role change applies from the next round onward. It never applies retroactively to a round already in progress.

**Hard rule: no self-review.** An engine that wrote any part of an artifact may never critique or review that artifact. Checking the current roles block is not enough, because roles can change mid-run. Track who actually wrote what:

- Whenever an engine writes or revises the plan, append its name to `.agentloop/runs/<run-id>/plan-authors`, if it is not already listed.
- Whenever an engine commits code for the run, including the Gate 3 fix pass, append its name to `.agentloop/runs/<run-id>/code-authors`.
- For every such commit, also record **who wrote that specific commit** in `.agentloop/runs/<run-id>/commit-authors`, one line per commit: `<full sha> <engine>`.
  - If both engines wrote part of one commit, for example a chunk one engine started and the other finished after a handoff (section 5.6), record `<sha> claude,codex`.
  - Never record a mixed commit as single-author.

Before invoking a Critic, check that it is not listed in `plan-authors`. Before invoking a Reviewer, check that it is not listed in `code-authors`. The `agentloop-critic` wrapper (section 8.3) also enforces this and refuses with exit code 6.

If both engines wrote code for the run, Gate 3 uses **split review** (section 9.3): each engine reviews only the commits the other wrote, and any commit both wrote goes to the human. For anything else that fails this check, for example both engines writing parts of the plan, stop and tell the human. Only the human decides how that work gets reviewed.

---

## 2. Overview

```
 task arrives
     │
     ▼
 triage (advisory) ── may recommend Gate 1 ──► human decides
     │
     ├── no gate ──► ungated worker does it (lint + tests) ──► re-triage the diff
     │
     ▼
 GATE 1  PLAN      Author drafts ⇄ Critic reviews   (AI-only: max 2 rounds, conditional 3rd)
     │             ▲                                 human reviews and may send it back:
     │             └──── human feedback cycle ────── unlimited cycles
     │
     ▼   human says "go to Gate 2"
 GATE 2  BUILD     Implementer only. TDD. Lint + tests after every chunk.
     │             No cross-model conversation.
     ▼
 GATE 3  REVIEW    Reviewer: ONE pass on the committed diff → Implementer: ONE fix pass
     │
     ▼
 report to human ──► human decides whether it ships
```

---

## 3. Triage (advisory only)

Triage never starts a gate on its own. Its only output is a recommendation to the human.

When a task arrives, compare it against the triage rules in `.agentloop/project-rules.md`. **Recommend Gate 1** if any of these apply:

- **Risk path:** the task is likely to touch a path or pattern listed in project-rules.
- **Irreversibility:** the task does something a revert cannot undo. This includes destructive data migrations, deleting data, sending messages or emails, publishing an API or package, and changing credentials.
- **Behavior change:** the task changes externally visible behavior, a default value, a config value, a dependency, or an access-control decision.
- **Size (secondary):** the task is likely to exceed 300 changed lines or 8 files. This threshold is adjustable in project-rules.

If any rule applies, say so in one or two sentences, name the rule, and ask: *"Recommend Gate 1 because <reason>. Run it?"* Then wait for the answer.

If no rule applies, proceed as an ungated task (section 7). The human can always say "run Gate 1" on any task regardless of triage.

---

## 4. Gate 1 — Plan

**Starts only when the human asks for it**, for example "write a plan for X and run it past Claude," or by accepting a triage recommendation.

### 4.1 The plan

First, save the human's request word for word to `.agentloop/runs/<run-id>/request.md`. It is the fixed reference that every version of the plan is checked against.

The Author then writes `.agentloop/runs/<run-id>/plan.md` with these sections:

1. **Goal:** what is being built and why, in plain language.
2. **Acceptance criteria:** numbered, observable conditions that mean "done," derived from `request.md`. Each one must be covered by at least one test in the test list. If the request is too vague to write these, ask the human (core rule).
3. **Non-goals:** what is explicitly out of scope.
4. **Assumptions and invariants:** facts the plan depends on. If any of these turns out false during Gate 2, that is a stop condition (section 5.3).
5. **Files and components:** what will change.
6. **Chunks:** the ordered implementation steps. Each chunk is small enough to test on its own.
7. **Test list:** for each chunk, the tests that prove it works, including failure cases and edge cases. This list is the TDD contract for Gate 2.
8. **Risks and rollback:** what could go wrong, and how it would be undone.

`<run-id>` is `YYYYMMDD-HHMM-<short-slug>`, for example `20260928-1430-add-login`.

### 4.2 The AI-only exchange (capped)

- **Round 1:** the Author writes a brief (section 8.1) and invokes the Critic in a fresh session. The Critic returns a structured verdict.
- **Snapshot.** After every round completes (outcome `ok` or `malformed`), copy the exact plan that round reviewed to `plan.critiqued.md` in the run folder. That file is always "the version the Critic last saw." A failed attempt never updates it, because the Critic saw nothing.
- The Author **verifies every finding** against the plan and the code before accepting it (section 6). Then the Author revises the plan.
- **Round 2** runs if any of these is true:
  - round 1 was not `APPROVED`;
  - round 1 left an open major or critical finding;
  - the plan has changed at all since the Critic saw it (`cmp -s plan.md plan.critiqued.md` fails).

  An `APPROVED` verdict does not cover edits made afterward.

  **Resuming.** If an earlier round in this cycle succeeded, the adapter in section 8.3 saved its session. Resume that **same** Critic session with a revision note listing, for each finding id, what changed or why the Author disputes it, with evidence. If no session was saved, for example because the earlier attempt failed or its output was unusable, start a **fresh** Critic session with the full brief (section 8.1). Never resume a session id that was not saved after a successful round.
- **Round 3 (conditional):** allowed once, and only when a specific major or critical finding is still open and the Author can name what a third round would resolve. Otherwise, do not run it.
- **Hard cap:** at most 3 Critic **rounds** per cycle. A round is an attempt that produced a critique, meaning its outcome is `ok` or `malformed`. Every attempt, successful or not, is logged to `.agentloop/runs/<run-id>/attempts.log` in the format shown in section 8.3, and the round count is computed from that log, never from memory. Never "just one more round."
- **Failed attempts** (outcome `env`, `timeout`, `quota`, or `error`) produced no critique, so they do not use up a round. They are **never retried silently**: stop, tell the human what failed, and ask before trying again. Every retry is logged, so no loop can hide.

Reaching the cap is **not** approval. Anything still open goes to the human.

**Unreviewed edits.** If the Author changes the plan after the Critic's last look and no round remains to review the change, the hand-off must show the difference (`diff -u plan.critiqued.md plan.md`), clearly labeled **"changed after the Critic's last review: not reviewed."** The human must always be told which parts of the plan the Critic did not see.

### 4.3 Hand-off to the human

After the AI-only exchange ends, present the following to the human:

- the current plan;
- a findings summary from `findings.md` showing what was confirmed and fixed, what the Author disputed and why, and what is still open. Start with its "Needs your attention" line;
- each open or disputed major or critical finding, **quoting the Critic's original claim word for word** next to the Author's position;
- any drift from the original request.

Then **stop and wait.**

### 4.4 Human feedback cycles (unlimited)

After the hand-off, the human takes one of two paths:

- **Approve.** Gate 1 ends. Record the approval in `decisions.md`. No further critique runs. Then wait for the human to explicitly start Gate 2 (section 4.5).
- **Send feedback.** This starts a new **cycle**, as follows.

For a new cycle:

- Save the feedback word for word to `decisions.md`.
- The Author revises the plan to incorporate it. **Human feedback is authoritative.** The Critic may flag a risk in it, but it cannot overrule it.
- Run a fresh AI-only exchange on the revised plan. This new exchange gets its own round cap of 2, plus the conditional 3rd.
- Hand off to the human again.

There is no limit on cycles. Only the AI-only rounds inside each cycle are capped.

### 4.5 Leaving Gate 1

An approved plan does **not** start Gate 2. Gate 2 starts only when the human explicitly says so, for example "go to Gate 2."

---

## 5. Gate 2 — Build

**Single engine: the Implementer.** No cross-model conversation happens at this gate. Quality comes from TDD, lint, and the coding standards below.

### 5.1 Setup

1. Confirm the working tree is clean (`git status`). If it is not, stop and ask the human. Never stash, reset, or discard their changes on your own.
2. Record the starting point:

   ```bash
   git rev-parse HEAD > .agentloop/runs/<run-id>/base-sha
   git rev-parse --abbrev-ref HEAD > .agentloop/runs/<run-id>/base-branch
   ```

   If `base-branch` comes out as `HEAD`, you are on a detached HEAD, so stop and ask the human which branch this work should eventually be merged into.
3. Create a work branch: `git switch -c agentloop/<run-id>`. Every agent commit for this run goes on this branch and nowhere else.

### 5.2 The TDD loop, per chunk

For each chunk in the plan, in order:

1. **Red:** write the tests from the plan's test list for this chunk. Run them. Confirm they fail, and that they fail for the expected reason.
2. **Green:** write the minimum code that makes them pass. Run the full test command from project-rules.
3. **Lint:** run the lint command from project-rules. Fix every error and warning.
4. **Checkpoint:** commit locally with the message `chunk <n>: <summary>`, then record the commit and its author:

   ```bash
   git rev-parse HEAD >> .agentloop/runs/<run-id>/run-commits
   echo "$(git rev-parse HEAD) <your engine>" >> .agentloop/runs/<run-id>/commit-authors
   ```

   Write `<your engine>` as `claude` or `codex`, or `claude,codex` if both engines wrote part of this commit (section 5.6).
5. Move to the next chunk only when tests and lint are both clean.

**Never** do any of the following without asking the human first:

- delete or weaken a test to make it pass;
- skip or mark tests as expected-to-fail;
- disable lint rules or add suppression comments;
- add a dependency;
- push, merge, or rewrite history.

**No real external effects.** Approving the plan authorizes *writing* the code, not *executing* real-world actions. While building and testing, never do any of the following:

- send real email, SMS, notifications, or webhooks;
- call live payment, billing, or other paid third-party APIs;
- run migrations or writes against any database other than a disposable local or test one;
- publish packages, deploy, or change cloud or DNS resources;
- read, print, or use real secrets or production credentials.

Tests must use fakes, mocks, sandboxes, or test-mode endpoints for all of these. If a step genuinely needs a real external action, stop and ask. The human must authorize that specific action at that moment. Record the authorization in `decisions.md`. This rule also applies to ungated tasks.

### 5.3 Drift rules

While implementing, you will find things the plan did not anticipate. Classify each one:

- **Safe addition:** it is not in the plan, but it is needed and it does not contradict any assumption, invariant, or non-goal. Examples: a helper function, a missing import, a small refactor inside a file already in scope. **Do it**, then log it in `drift.md` as "not in plan: <what> — <why>". Report every entry to the human at the end.
- **Breaking or uncertain:** it contradicts a plan assumption or invariant, touches a risk path the plan did not cover, or you are not sure whether it is safe. **Stop the run immediately.** Do not make assumptions and do not improvise a workaround. Tell the human what you found, why it matters, and the options you see. Then wait. The human decides whether to continue, amend the plan, or reopen Gate 1.

When in doubt, treat it as breaking.

### 5.4 Coding standards

The human must be able to read and understand the code you write. Prefer these:

- Follow the existing conventions of the codebase before your own preferences.
- Use clear, descriptive names. Keep functions small with a single purpose. Keep nesting shallow.
- Write no dead code, commented-out code, or speculative abstraction. Build what the plan needs, not what it might need later.
- Write comments only where they explain *why*, never *what*.
- Handle errors deliberately at system boundaries. Do not swallow them silently.
- Name tests by the behavior they check.

### 5.5 Finishing Gate 2

1. Run the full test suite and lint one last time. Both must be clean.
2. Commit, recording the commit in `run-commits` and `commit-authors`. Then run `tree_is_clean` (section 9.1). If anything is still uncommitted or untracked, it would silently fall outside the review, so stop and ask the human. Do not commit, stash, or delete it yourself. Only when the tree is clean, record the head with `git rev-parse HEAD > .agentloop/runs/<run-id>/reviewed-sha` and run `range_is_isolated`. If that check fails, the branch contains commits this run did not make, so stop and ask the human.
3. **Re-triage the real diff** (`git diff <base-sha>..<reviewed-sha>`) against the triage rules. Note anything the real diff touches that the plan did not predict.
4. Proceed to Gate 3.

### 5.6 Handing off mid-gate (an engine hits its usage limit)

Engines run out of usage mid-run. When that happens, the run continues from its files. Do not start over: the plan, decisions, findings, drift log, and commits hold everything the previous engine knew.

**At Gate 2** (the Implementer ran out):

1. **The human decides:** wait for the reset, or hand the rest of the gate to the other engine. Show the reset time if the engine printed one. If the reset is soon, waiting keeps authorship single, so Gate 3 stays a normal review.
2. **If handing off,** the new engine records a `Role change` block in `decisions.md`: who takes over, from which chunk, and why.
3. **Before writing anything, report the state to the human:**
   - which chunks are committed (`run-commits`);
   - what is uncommitted;
   - whether tests and lint pass;
   - what is left.
4. **Never discard unfinished work.** A half-done chunk sits uncommitted in the working tree. Do not `checkout`, `reset`, `stash`, or delete it. Read it, compare it against that chunk's plan and test list, and finish it.
5. **Record authorship honestly.** A chunk the old engine started and the new engine finished is recorded in `commit-authors` as `claude,codex`. Every commit after that is recorded under the new engine.
6. **Continue the TDD loop** from where it stopped, with the same plan and the same rules.

At Gate 3, both engines will then appear in `code-authors`, so Gate 3 uses split review (section 9.3).

**At Gate 1** (the Author ran out): prefer waiting, because Gate 1 is short. If the human hands off anyway, the new engine becomes a plan author too. Then neither engine can critique the plan any further, and the remaining plan review falls to the human.

**When a reviewer runs out** (Critic or Reviewer): that is a failed attempt (`quota`, section 8.4). No round is used, nobody else takes over the review, and the human decides whether to wait or proceed.

---

## 6. Handling findings (Gates 1 and 3)

Every finding from the Critic or the Reviewer goes into `.agentloop/runs/<run-id>/findings.md`, in the format in section 6.1.

- **Verify before accepting.** Check each claim against the actual plan or code, and check the behavior, not just whether the cited line exists. Reviewers sometimes cite lines that do not say what they claim.
- Classify each finding as `confirmed` (it will be fixed), `disputed` (with concrete evidence for why it is wrong), or `needs-human`.
- **A disputed major or critical finding always goes to the human.** The author of the work may not quietly discount criticism of its own work.
- Never present an unverified finding to the human as fact.
- Status is decided from the revised artifact, not from the reviewer's own claim that something is resolved.

`findings.md` and `decisions.md` are written **for the human to read**. Follow the templates below exactly.

- Wrap prose at about 80 to 100 characters.
- Never write one long line per entry.
- Do not use tables for long text. A table is allowed only for the short overview.

### 6.1 `findings.md` format

The file has an overview first, then one block per finding. Within each status group, sort blocks by ID.

Status groups, in this order:

1. `needs you`
2. `open`
3. `disputed`
4. `fixed`

```markdown
# Findings: Gate 1, cycle 2

**Needs your attention:** C2-3 (disputed, major).
10 findings: 3 major and 7 minor. 8 fixed, 1 needs you, 1 disputed.

| ID   | Severity | In plain words                               | Status    |
|------|----------|----------------------------------------------|-----------|
| C2-3 | major    | Plan never decided how files reach the trash | disputed  |
| C2-2 | major    | Program loops forever if input ends (Ctrl+D) | fixed     |

---

### C2-3 · major · disputed

**In plain words:** The plan never decided how files get moved to the trash
when they are on a different drive.

**What the Critic said (verbatim):**
> The plan requires 'cross-filesystem cases' and 'recoverable Linux desktop
> trash' but does not decide the mechanism. [...]

**Where:** plan.md, Assumptions; AC 7; chunk 4; test T10

**Author's assessment:** disputed. <The concrete evidence for why the claim
is wrong, citing the plan or the code.>

**How it was addressed:** Not changed. This is waiting for your decision.

**Status:** disputed. It needs your decision, because it is a major finding.
```

**Rules for each block:**

- **ID:** use `C<N>-<n>` for Gate 1 cycle N, and `G3-<n>` for Gate 3.
- **In plain words:** one or two sentences a non-expert can follow. It must state the problem **as seriously as the reviewer did**. The author of the work may not soften criticism of its own work.
- **What the Critic said:** the claim copied **word for word** as a quote. Wrapping it across lines is fine; changing its words is not. Use `[...]` only to cut text, never to reword it.
- **Author's assessment:** `confirmed`, `disputed`, or `needs-human`, plus the evidence.
- **How it was addressed:** what changed and where, or why nothing changed.
- **Status:** `fixed`, `open`, `disputed`, or `needs you`. For `fixed`, say which round confirmed it.

**Keep the overview current.** Update the overview and the "Needs your attention" line every time a block's status changes. If nothing needs the human, say so: "**Needs your attention:** none."

### 6.2 `decisions.md` format

The file is a log with one block per event, **oldest first**. Each heading gives the **date and clock time**, the type of event, and a short title.

Event types:

- `Your answers`
- `Your feedback`
- `Approval`
- `Authorization`
- `Role change`
- `Gate failure`

```markdown
# Decisions: <run-id>

## 2026-10-01 23:27 · Your answers: after Critic round 1

1. If input ends (Ctrl+D), exit without making moves?
   → **Yes, exit safely**
2. Files on another filesystem?
   → **Use that filesystem's trash, or fail safely**

You said, word for word: "Do all of the recommended for the three questions"

## 2026-10-01 21:51 · Gate failure: Critic round 1 did not run

- **What failed:** the Claude call could not reach the API (`EAI_AGAIN`).
- **Why:** it ran inside the network-restricted sandbox.
- **Effect:** no critique was produced, so no round was used. The gate is uncleared.
- **Next:** waiting for you to approve a retry outside the sandbox.
```

**Rules for each block:**

- **Questions:** list each one with the option chosen, in bold, on its own line.
- **Your words:** always end with the human's reply **word for word**.
- **Gate failures:** use the four labeled lines shown: what failed, why, effect, next.
- **Nothing is ever removed.** Correct a mistake with a new block that refers to the old one.

---

## 7. Ungated tasks

These are tasks that triage did not flag and that the human did not send through Gate 1.

The **ungated worker** does the task alone, in the human's working tree.

**Ungated tasks never get a Gate 3 review.** This workflow cannot reliably separate the worker's edits from the human's edits to the same files in a shared working tree, so it does not pretend to review ungated work in isolation. Any task that needs a cross-model review goes through Gate 1 and Gate 2 instead.

- If the request is unclear, ask before starting (core rule).
- Wherever behavior changes, write or update a test first, and keep tests and lint clean.
- **Do not commit.** Leave the changes in the working tree for the human to review and commit.
- **When done, report to the human:**
  - every file you changed;
  - the final test and lint status;
  - whether the finished change now matches a triage rule. If it does, say: *"This turned out to touch <X>. If you want a cross-model review, I recommend rerunning it through Gate 1."* The human decides.

---

## 8. Invoking the other engine

### 8.1 Briefs

A brief is a self-contained markdown file in the run folder. The invoked engine has not seen this conversation. A brief contains:

- **Your role:** "You are the Critic (Gate 1)" or "You are the Reviewer (Gate 3)." Follow `AGENTS.md` section 8 only. Do not edit anything.
- **The goal and acceptance criteria.** Where they come from depends on the gate:
  - **Gate 1:** paste `request.md` word for word, plus the draft plan's acceptance criteria. Ask the Critic to also check that those criteria faithfully and completely reflect the request.
  - **Gate 3:** the approved plan's acceptance criteria.

  If the required source does not exist, do not invoke the other engine. Ask the human first.
- **What to review:** the plan file at Gate 1, or the diff file at Gate 3.
- **What to focus on:**
  - At Gate 1: correctness, missing cases, wrong assumptions, risk, testability.
  - At Gate 3: readability, code quality, plan conformance, edge cases the tests miss, and bugs.
- **No assumptions:** if something in the artifact is ambiguous or underspecified, do not guess what was meant. Report it as a finding, with severity set by its impact, and name the question that needs answering. If it blocked your review of an area, list that area under `not_assessed`. You cannot ask the human directly; the invoking engine takes your questions to them.
- **Output rules:** return the verdict schema. Mark as `critical` or `major` only the issues that would cause wrong behavior, data loss, or a security problem, or that would make the code hard to maintain. List what you reviewed and what you could not assess.
- Any context the reviewer needs that is not in the repo, pasted inline. The reviewer may have no network access.

**Anti-anchoring rule.** A Gate 3 brief must **not** contain the Implementer's own opinion of the diff, its confidence, or a list of issues it already suspects. At Gate 1, the plan is necessarily visible, but the brief must still leave out the Author's self-assessment.

### 8.2 Verdict schema

Write this schema once to `.agentloop/verdict.schema.json`, during Step 0 or Discovery:

```json
{
  "type": "object",
  "required": ["verdict", "summary", "findings", "reviewed", "not_assessed"],
  "additionalProperties": false,
  "properties": {
    "verdict":  { "enum": ["APPROVED", "REVISE", "BLOCKED"] },
    "summary":  { "type": "string" },
    "reviewed": { "type": "array", "items": { "type": "string" } },
    "not_assessed": { "type": "array", "items": { "type": "string" } },
    "findings": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["id", "severity", "claim", "location", "suggestion", "addresses"],
        "additionalProperties": false,
        "properties": {
          "id":         { "type": "string" },
          "severity":   { "enum": ["critical", "major", "minor"] },
          "claim":      { "type": "string" },
          "location":   { "type": "string" },
          "suggestion": { "type": ["string", "null"] },
          "addresses":  { "type": ["string", "null"] }
        }
      }
    }
  }
}
```

`APPROVED` means "no blocking findings within the stated scope." It never means "safe to ship."

Every property is listed as required, and optional values are expressed as `null`. OpenAI's structured-output mode, which Codex uses, rejects a schema with any optional property. Keep this form if you edit the schema.

### 8.3 Invoking the other engine: the `agentloop-critic` wrapper

**Always** invoke the Critic or the Reviewer through this one command. Never call `claude` or `codex` yourself for a review.

```
/home/rfloros/agent-workflow/bin/agentloop-critic --run .agentloop/runs/<run-id> --gate gate1 --engine <critic> --cycle <N>
/home/rfloros/agent-workflow/bin/agentloop-critic --run .agentloop/runs/<run-id> --gate gate3 --engine <reviewer>
/home/rfloros/agent-workflow/bin/agentloop-critic --run .agentloop/runs/<run-id> --gate gate3 --engine <engine> --split
```

`<critic>` and `<reviewer>` are `claude` or `codex`, taken from the Roles block. The third form is split review (section 9.3), used only when both engines wrote code for the run.

**Why a wrapper.** Every safety flag is fixed inside the script, and the script lives outside every project. That is what makes it safe to pre-approve (section 12). Adding arguments cannot widen what it does: it rejects anything it does not recognize, and a sandboxed agent cannot edit it.

**How to call it.** These rules are not optional. They are what the pre-approval matches.

- Use the **absolute path** shown.
- Run it **from the project root**.
- Run it as a **plain command**: nothing before or after it, no `cd … &&`, no redirection, no variables.
- Any other shape will stop for approval, and inside Codex's sandbox it will fail.

**Never** edit or work around any of these:

- the wrapper itself (`~/agent-workflow/bin/`);
- the Codex rule file (`~/.codex/rules/agentloop.rules`);
- the Claude permission settings (`~/.claude/settings.json`).

If the wrapper misbehaves, stop and tell the human.

**Steps for each review:**

1. **Ask the wrapper for the tag.** Add `--print-tag` to the same command. It prints the name for this attempt and calls nothing:
   - at Gate 1, `c<N>-r<M>`, where `M` is one more than the rounds already completed in this cycle;
   - at Gate 3, `g3`, or `g3-claude` / `g3-codex` in split review.

   If it says `CAP REACHED`, stop and report to the human.
2. **Write the brief** to `.agentloop/runs/<run-id>/brief-<tag>.md` (section 8.1).
   - If this cycle has no saved session (no `critic-session-c<N>` file), the reviewer remembers nothing, so write the **full** brief.
   - Write only a revision note when resuming.
3. **Gate 3 only:** first pin the target. Both engines review this exact file:

   ```bash
   git diff "$(cat .agentloop/runs/<run-id>/base-sha)..$(cat .agentloop/runs/<run-id>/reviewed-sha)" > .agentloop/runs/<run-id>/g3-diff.patch
   ```

   The brief names `g3-diff.patch` as the only thing under review. Never use Codex's `review --base` or `review --uncommitted`, because those follow a moving branch or working tree.

   **In split review, skip this step.** The wrapper builds each reviewer's patch itself, `g3-<engine>-diff.patch`, from `commit-authors`, so nobody picks the commits by hand. The brief names that file.
4. **Run the wrapper** without `--print-tag`. It ends with one line:
   `RESULT tag=<tag> outcome=<outcome> verdict=<path to the verdict file>`

**What the wrapper does, so you do not have to:**

- **Refuses self-review.** It will not run a reviewer that is listed in `plan-authors` (Gate 1) or `code-authors` (Gate 3). Exit code 6. When both engines wrote code, the refusal says to use split review.
- **In split review, sends each reviewer only the other engine's commits.** It reads `commit-authors`, never includes a commit the reviewer wrote even in part, and lists every commit both engines wrote as `HUMAN REVIEW NEEDED`. It refuses (exit 6) in three cases:
  - a commit in the range has no recorded author;
  - an author entry is malformed;
  - there is nothing for this reviewer to review.
- **Counts rounds from `attempts.log` and enforces the cap.** A round is an attempt that produced a critique (`ok` or `malformed`). Exit code 3 at the cap.
- **Runs the reviewer read-only, with the brief on stdin.** The brief is never a command argument, so its text can never be read as a flag.
  - Claude runs in plan mode with `Edit`, `Write`, `NotebookEdit`, `Bash`, `WebFetch` and `WebSearch` denied.
  - Codex runs with `--sandbox read-only`.
- **Resumes only a session that an earlier successful round saved,** and only if it is a well-formed id. Otherwise it starts fresh.
- **Forgets a saved session if continuing it fails for any reason.** Claude reports "No conversation found" and Codex reports "no rollout found", but the rule does not depend on either wording. The retry then starts fresh.
- **Validates the verdict against the full schema** (section 8.2).
- **Logs every attempt and its outcome** to `attempts.log`, one line per event: `<ISO timestamp> <tag> engine=<name> <started|ok|malformed|timeout|quota|env|error>`.
- **Saves the session id only after an `ok` round.** A failed call can return an id for a conversation that was never saved, and the wrapper never keeps such an id.

Exit codes:

| Code | Meaning |
|---|---|
| 0 | `ok` |
| 10 | `malformed` |
| 11 | a failed attempt: `env`, `timeout`, `quota`, or `error` |
| 3 | round cap reached |
| 6 | self-review refused |
| 2 | usage error |

### 8.4 What each outcome means, and what to do

- **`ok`:** a valid critique. It used a round. Read the verdict file and handle each finding (section 6).
- **`malformed`:** the reviewer ran but its output failed the schema check. It used a round, and no session was saved. If a round remains, the next one starts fresh with the full brief.
- **`env`, `timeout`, `quota`, `error`:** no critique was produced, and no round was used. The gate stays **uncleared**. Then:
  1. Record it in `decisions.md` as a `Gate failure` block (section 6.2).
  2. Tell the human plainly, for example: "Gate 1 critique did not run: no network (EAI_AGAIN)."
  3. **Ask before retrying.** Never retry silently, and **never substitute your own self-review and call the gate passed.**
  4. For `env`, the likely cause is that the wrapper was not run as a plain command, so the pre-approval did not apply. Check how you called it.
  5. For `quota`, the wrapper prints a `USAGE LIMIT:` line, for example "You've reached your Claude usage limit. It resets at 3:00 PM on Fri Oct 2." **Show that line to the human word for word.** If the error did not include a reset time, the line says so and quotes the original message. The human decides whether to wait for the reset or to proceed with the gate marked unreviewed.

A retry after a failed attempt is a fresh session, unless this cycle already has a saved session from an earlier successful round.

---

## 9. Gate 3 — Review

Gate 3 runs automatically at the end of Gate 2. Only tasks that went through Gate 1 and Gate 2 get a Gate 3 review. Ungated tasks never do (section 7).

### 9.1 Isolation checks

These checks make sure the Gate 3 review covers exactly this run's work: nothing extra, and nothing left out.

```bash
# Nothing is left out: no uncommitted or untracked changes remain.
tree_is_clean() {
  [ -z "$(git status --porcelain --untracked-files=all)" ]
}

# Nothing extra: every commit in base..reviewed must be one this run recorded.
range_is_isolated() {
  diff <(git rev-list "$(cat "$RUN/base-sha")..$(cat "$RUN/reviewed-sha")" | sort) \
       <(sort -u "$RUN/run-commits") >/dev/null
}
```

These rely on `.agentloop/runs/` being gitignored (section 10), so the run folder itself never shows up as a change.

### 9.2 Steps

1. **Pin the target.** Run `tree_is_clean` and `range_is_isolated`. If either fails, stop and ask the human. Otherwise, write `base-sha..reviewed-sha` to `g3-diff.patch` (section 8.3). That file is the only thing under review, for either engine.
2. **One review pass.** Write the Gate 3 brief following the anti-anchoring rule, then invoke the Reviewer once in a fresh session. There is no second review round. A failed attempt is logged, and the human decides whether to retry.
3. **Verify** every finding (section 6).
4. **One fix pass.** The Implementer fixes the confirmed findings. It then runs tests and lint, which must be clean, commits, and records the commit in `run-commits` and `commit-authors`.
5. **Record changes made after review.** Anything committed after `reviewed-sha` was not seen by the Reviewer. List it in the report as "changed after review."
6. **Report to the human** in `report.md`:
   - what was built;
   - the findings and how each was handled;
   - disputed and `needs-human` items, with the Reviewer's claims quoted word for word;
   - the drift log;
   - the changes made after review;
   - the final test and lint status;
   - **the exact commands for the human to ship it**, filled in with the real branch names, for the human to run themselves:

     ```bash
     git switch <base-branch>
     git merge --no-ff agentloop/<run-id>
     git push origin <base-branch>
     git branch -d agentloop/<run-id>    # optional cleanup after merging
     ```

If Gate 3 turns up something serious enough that one fix pass cannot resolve it, **do not loop.** Report it. It usually means the plan needs to be reopened at Gate 1, and that is the human's call.

**The run ends with the report.** The agent never runs the shipping commands (section "The human controls shipping").

### 9.3 Split review (both engines wrote code)

Use this instead of the single review in 9.2 when `code-authors` lists both engines, typically after a mid-gate handoff (section 5.6).

- **Step 1 of 9.2:** its checks (`tree_is_clean`, `range_is_isolated`) still apply. The per-engine patches the wrapper builds replace `g3-diff.patch`.
- **Steps 3 to 6 of 9.2:** apply unchanged.
- **Step 2 of 9.2** becomes:

1. **Run two reviews, one per engine.** For each engine:
   - get its tag with `--split --print-tag` (`g3-claude` or `g3-codex`);
   - write `brief-g3-<engine>.md`, naming `g3-<engine>-diff.patch` as the only thing under review;
   - run the wrapper with `--gate gate3 --engine <engine> --split`.

   The wrapper sends each engine only the commits the **other** engine wrote, so no engine ever reviews its own work.
2. **Commits both engines wrote** are reviewed by neither. The wrapper lists each one as `HUMAN REVIEW NEEDED`. Put them in the report under a heading **"Not reviewed by either engine: needs your review"**, with the commit, its subject, and what it changed.
3. **Each review is one pass.** If one engine is unavailable, for example still over its usage limit, run the other review now. Tell the human that the remaining part is unreviewed until that engine is back or the human reviews it.
4. **One fix pass covers both reviews.** It is done by the current Implementer and recorded in `run-commits` and `commit-authors` as usual.

---

## 10. Discovery (first run in a new project)

Run this when `.agentloop/project-rules.md` does not exist. Whichever engine the human started the session with runs it. Critic and Reviewer sessions never run Discovery.

**First, check that the reviewer wrapper is installed:** `/home/rfloros/agent-workflow/bin/agentloop-critic` must exist and be executable. If it is missing, stop and tell the human, because Gates 1 and 3 cannot run without it. Do not create or edit it yourself (section 8.3).

0. **Check that this is a git repository** (`git rev-parse --is-inside-work-tree`). If it is not, stop and ask the human: *"This folder isn't a git repository. Gate 2 needs one. Run `git init` here?"* Do not initialize it yourself without a yes. If the folder is empty or nearly empty, which is normal for a brand-new project, skip the scan in step 1. Instead, ask the human what language and framework the project will use, and write minimal rules from the seed list in step 2. Re-run Discovery once real code exists.
1. Scan the repo: its languages, frameworks, package manifests, directory layout, CI config, database and migration tooling, and auth or session code. Check whether it handles payments, sends email or notifications, has infrastructure config, or holds secrets and environment files.
2. Start from this seed list of risk categories and keep only the ones that actually exist in this repo, naming their real paths:
   - database migrations and schema;
   - auth, sessions, and permissions;
   - payments and billing;
   - outbound messaging (email, SMS, webhooks);
   - public API contracts;
   - dependency manifests and lockfiles;
   - CI/CD and deploy config;
   - infrastructure-as-code;
   - secrets, env files, and security config;
   - data deletion or retention.
3. Add **pattern rules** for diffs, meaning lines matching things like `DEBUG|SECRET|TOKEN|PASSWORD|TIMEOUT|RETRY|RATE_LIMIT|feature.?flag`, and any deletion under the test directories.
4. Find the project's **lint command** and **test command**. If either is missing, **do not install anything.** Propose one to the human and wait, because adding a dependency is itself a gated change.
5. Write `.agentloop/project-rules.md` containing:
   - the risk paths;
   - the pattern rules;
   - the size threshold (default: 300 lines or 8 files);
   - the lint command;
   - the test command.

   Also write `.agentloop/verdict.schema.json`.
6. Add `.agentloop/runs/` to `.gitignore`. Keep `project-rules.md` tracked so the rules evolve with the project.
7. **Claude Code hookup.** Claude Code does not read `AGENTS.md` on its own. If there is no `CLAUDE.md` in the repo root, offer to create one containing the single line `@AGENTS.md`. If one already exists, offer to append that line to it.
8. Show the human the rules file and ask them to confirm or edit it before any task begins.
9. **Commit the setup.** Once the human confirms, ask: *"Commit the setup files?"* On a yes, run:

   ```bash
   git add AGENTS.md .agentloop/project-rules.md .agentloop/verdict.schema.json .gitignore
   git add CLAUDE.md    # only if it was created or edited
   git commit -m "chore: add agentloop project rules"
   ```

   This is required. Gate 2 refuses to start on a dirty working tree, and in a brand-new repository this commit is also the first one, which Gate 2 needs as its base. `AGENTS.md` is included because a freshly copied, untracked `AGENTS.md` would make the tree dirty. If it is already committed, adding it again does nothing.

Re-run Discovery whenever the human asks, for example after adding a payments feature.

---

## 11. Run folder layout

```
.agentloop/
  project-rules.md           # tracked: triage rules, lint and test commands
  verdict.schema.json        # tracked
  runs/                      # gitignored
    <run-id>/
      request.md             # Gate 1: the human's request, word for word
      run-commits            # every commit this run made
      plan.md                # current plan
      plan.critiqued.md      # exact plan version the Critic last reviewed
      plan.c<N>.md           # snapshot at the end of each cycle
      attempts.log           # every Critic/Reviewer attempt and its outcome
      plan-authors           # engines that wrote any part of the plan
      code-authors           # engines that committed any code for this run
      commit-authors         # one line per commit: "<sha> claude|codex|claude,codex"
      brief-<TAG>.md         # briefs: TAG = c<N>-r<M> (Gate 1) or g3 (Gate 3)
      <TAG>.raw.json[l]      # raw engine output
      <TAG>.err              # engine stderr
      <TAG>.verdict.json     # validated verdict
      critic-session-c<N>    # Critic session id for resuming within cycle N
      findings.md            # findings, human-readable: overview + one block each (6.1)
      decisions.md           # timestamped log: answers, feedback, failures (6.2)
      drift.md               # Gate 2 safe additions not in the plan
      base-sha               # Gate 2 base commit
      base-branch            # branch the work will be merged into (by the human)
      reviewed-sha           # commit reviewed at Gate 3
      g3-diff.patch          # the pinned diff the Reviewer read
      g3-<engine>-diff.patch # split review: only the other engine's commits (built by the wrapper)
      report.md              # final report to the human
```

---

## 12. Setup notes and open items to verify on first real run

- **Claude Code and AGENTS.md.** Codex reads `AGENTS.md` natively. Claude Code needs a `CLAUDE.md` containing `@AGENTS.md`, which Discovery offers to create. Until that file exists, start sessions with Codex, or tell Claude explicitly to "read AGENTS.md and follow it."
- **Pre-approval, so reviews run without stalling.** The reviewer call needs network access. Codex's sandbox blocks the network, and that cost about 27 minutes of waiting on a prompt in the first real run. Both directions are pre-approved for **only** the wrapper command, set up by the human, once per machine:
  - **Codex as Author:** `~/.codex/rules/agentloop.rules` holds a `prefix_rule` allowing the wrapper's absolute path, with `decision = "allow"`. That runs it outside the sandbox without prompting.
    - Check it: `codex execpolicy check --rules ~/.codex/rules/agentloop.rules -- /home/rfloros/agent-workflow/bin/agentloop-critic --run x` should report `allow`, and `-- claude -p x` should match nothing.
  - **Claude as Author:** `~/.claude/settings.json` has `permissions.allow` entries for `Bash(/home/rfloros/agent-workflow/bin/agentloop-critic *)` and the `~/` form.

  This is safe only because the wrapper's flags are fixed inside it, and the wrapper sits outside every project, where no sandboxed agent can edit it. **Never broaden either rule to plain `claude` or `codex`.** Prefix rules match only the start of a command, so a broader rule would also allow dangerous flags appended to the end.
- **Verified in v2.7:** with the rule in place, Codex (`workspace-write` sandbox, `approval: never`) ran the wrapper and Claude's critique completed (`ok`). Claude as Author, with permission prompts disabled, ran the wrapper and Codex's critique completed (`ok`, 0 denials).
- **Moving to another machine or username:** the absolute path `/home/rfloros/...` appears in section 8.3, in the Codex rule file, and in the Claude permission settings. Update all three together.
- **Verified in v2.1:** the Codex session id is `thread_id` in the `thread.started` event, and `codex exec --sandbox read-only resume <thread_id>` restores the conversation.
- **Still unverified:** the exact text each CLI prints when a subscription usage limit is hit.
  - The wrapper recognizes common wording: `rate limit`, `quota`, `usage limit`, `limit reached`.
  - It reads the reset time from three common forms: a `|<timestamp>` suffix, "try again in 2 hours 13 minutes", and "resets 3pm". All three were tested against stand-ins.
  - If a real message uses a form it doesn't recognize, nothing is hidden: the `USAGE LIMIT:` line quotes the original text instead.
  - If a limit is ever labeled `error` instead of `quota`, the wording was unrecognized. The gate is still uncleared and the human is still told. Add the wording to the wrapper's quota pattern.
