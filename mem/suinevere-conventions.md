---
name: suinevere-conventions
description: Suinevere's house style across the Saturn ports — banner file/symbol comments and the spec-then-plan-then-code workflow.
metadata:
  type: feedback
---

Match these when writing code or docs in Another-Saturn — they are consistent across
every file in [[zaturn-port-blueprint]].

**Why:** the zaturn codebase is uniformly commented in one distinctive format, and its
`docs/` shows a strict spec→plan→code cadence. New Saturn work should be
indistinguishable from it.

**How to apply:**

## Banner comments

Every file opens with, and every non-trivial symbol (macro, struct, function) is preceded
by, a banner of this exact shape:

```c
/*----------------------
 | <file or symbol name>
 | Description: <what it is and, crucially, WHY — the constraint, the bug it avoids,
 |   the ordering requirement. Continuation lines indent three spaces.>
 | Author: suinevere
 | Dependencies: <headers/modules, or "none">   <-- file banners only
 ----------------------*/
```

The Description field carries real rationale, not restatement: e.g. `STORY_READ_CHUNK`
explains why 8 sectors and not more, and `main`'s banner explains why the re-entry path
hand-clears state after a `longjmp`. Inline comments follow the same rule — they explain
why an ordering or a workaround exists, often naming the symptom that appeared without it.

## Spec → plan → code

Work is documented under `docs/superpowers/` as a pair of date-prefixed files:
`specs/YYYY-MM-DD-<topic>-design.md` then `plans/YYYY-MM-DD-<topic>.md`. Specs carry
`**Date:** / **Status:** / **Target engine:**` headers and sections for Goal,
Architecture (a file→language→responsibility table), Components, Data/control flow,
Memory, Deferred/stubbed, Build & test, Risks & mitigations, Out of scope.
Another-Saturn has no `docs/` yet.

## C / C++ split

C for portable core logic (`.c`), C++ for anything touching SRL (`.cxx`), bridged by an
`extern "C"` `*_glue.h`. Host-buildable logic gets plain-gcc unit tests under
`saturn/tests/`.
