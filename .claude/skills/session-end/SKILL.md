---
name: session-end
description: End a FourSwordsRecomp session - write the local handoff (.claude/HANDOFF.local.md) and update PROGRESS only for lasting changes. Invoke manually (/session-end).
disable-model-invocation: true
---

# Session end

Reply to the maintainer in Spanish. Never commit, push, run `/clear` or close the session.

1. Inspect the real state of the work and of Git (`git status`, branch, last commit).
2. Separate: completed, pending, blocked, unverified. Never mark a task done that was not verified.
3. Note important decisions and discarded approaches with their reasons, when relevant.
4. Create or update `.claude/HANDOFF.local.md` (gitignored) with:
   - Update date, branch, last commit
   - Session goal and current state
   - Relevant modified files
   - Results and verifications actually performed
   - Errors and blockers
   - Important decisions
   - Recommended next action
   - Constraints for the next session
5. Keep it concise (preferably under 800 tokens) without losing critical information. If the work is incomplete, record the exact point to resume from. If a previous handoff has still-relevant pending items, keep them; drop the rest.
6. Update `PROGRESS.md` and `PROGRESS.es.md` (kept in sync) only for lasting progress, status changes or new relevant pending items. Keep dated, verified/unverified wording per CLAUDE.md.
7. Do not duplicate technical knowledge already documented; link to it instead.
8. Never store credentials, secrets, sensitive file contents or full conversations.
9. Tell the user which changes are pending to be saved in Git.
