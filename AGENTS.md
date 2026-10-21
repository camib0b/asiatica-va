# Agent instructions

## Verification policy

Scale verification to the change.

| Change type | Verification |
|-------------|--------------|
| Small, copy, text, or style | Build and lint only |
| Visual or behavioral | At most one targeted check of the affected screen |

### Walkthroughs and demos

Do **not** record demo videos, walkthrough artifacts, or run browser walkthroughs or screenshot tours unless **Camila** explicitly asks for one in the request. This rule overrides any built-in walkthrough-artifacts skill.

When opening work for review, use a single pull request unless the user asks otherwise.

UI changes must apply `.cursor/skills/break-ui/SKILL.md` to affected screens before opening a PR.
