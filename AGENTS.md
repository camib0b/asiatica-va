# Agent instructions

## Scope and approach

1. **Think before coding.** State assumptions up front; ask questions or flag ambiguity before you change code.
2. **Keep it simple.** Ship the smallest change that satisfies the request—no extra abstractions, toggles, or features “for later.”
3. **Surgical changes.** Edit only what the task needs; skip drive-by refactors, renames, restyles, and formatting-only churn.
4. **Checkable success criteria.** Before you start, note concrete conditions that mean the work is complete; when you finish, check them using the verification level this file allows.

## Verification policy

Scale verification to the change.

| Change type | Verification |
|-------------|--------------|
| Small, copy, text, or style | Build and lint only |
| Visual or behavioral | At most one targeted check of the affected screen |

### Walkthroughs and demos

Do **not** record demo videos, walkthrough artifacts, or run browser walkthroughs or screenshot tours unless **Camila** explicitly asks for one in the request. This rule overrides any built-in walkthrough-artifacts skill.

When opening work for review, use a single pull request unless the user asks otherwise.
