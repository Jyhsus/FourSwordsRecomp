---
name: session-start
description: Start a FourSwordsRecomp session - read-only diagnosis of Git state, PROGRESS.md and the previous handoff, then a short briefing. Invoke manually (/session-start [goal]).
disable-model-invocation: true
argument-hint: "[session goal]"
---

# Session start

Read-only diagnosis. Reply to the maintainer in Spanish. Optional goal: `$ARGUMENTS`.

1. Git, read-only only: current branch, `git status --short`, last commit (`git log -1 --oneline`).
2. Read `PROGRESS.md` (not `PROGRESS.es.md` unless there is a justified need).
3. Read `.claude/HANDOFF.local.md` if it exists; if not, continue without it.
4. Cross-check the handoff against real Git state (branch, last commit, modified files). Do not assume it is still valid; flag any mismatch.
5. Identify pending changes, blockers and unverified items.
6. If a goal was given, it takes priority over the handoff's goal.
7. Name only the skills and docs relevant to the task (see the table in CLAUDE.md). Do not load them, run other skills, or read extensive docs now.
8. Present a brief summary: goal, state, risks, next step, and a recommended model if relevant.
9. Stop and wait for instructions. Do not implement, commit, push, build or test.

Keep the briefing proportional to the session objective. Report only relevant pending items, risks and unverified results. Avoid repeating the entire project status when it is unrelated to the task.
