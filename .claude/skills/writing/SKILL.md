---
name: writing
description: How to write code comments and prose docs for plumb. Load before writing or editing any comment, Doxygen block, README, or DECISIONS file — including in tests, benches, and CMake.
---

Soft-wrap everything. One line per paragraph, let the editor wrap. Never hard-wrap prose.

# Clarity first

Applies to every comment, doc and chat reply. The reader should get it on one skim. If a sentence needs rereading, rewrite it.

- Say what literally happens, in plain words. No metaphor, idiom or insider shorthand.
- Name the concrete thing, unless a looser word is defined first. One word for one thing, throughout.
- A term the reader may not know gets one plain sentence before it is used.
- An example is marked as an example, or left out. A named case reads as the rule.
- Lead with the point. First sentence says what is done or what was found. Detail after, only if the reader needs it to follow.
- Leave out detail that lives in the code or another doc. No restating mechanics the reader did not ask for.
- Short beats complete. Two clear sentences beat a full paragraph.

# Code comments

## The one rule

A comment's substance lives in exactly one place: the comment itself. It describes only the code it sits on. Never reference other code, other files, docs, patterns, or anything not present in what's being commented.

## Length and style

- Max 4 lines. If you need more, you're writing prose, not a comment.
- Terse. Short. Not sentences-as-essay.
- No excessive punctuation. Em dashes, hyphens, semicolons, colons are a smell — reaching for them means the comment is too long. Cut it down.

## Banned

- Rationale-of-choice comments. No "std::array not vector because x, y, z." No "we do it this way because." The reason is not the comment's job.
- References to other code, files, docs, or patterns. No "follows the X pattern," no "see Y," no "as in Z."
- Restating what the code plainly says.
- Redundancy. If the same fact is already stated somewhere, don't repeat it.

## Header brief (Doxygen)

On class declarations, public interfaces, and especially concepts.

The brief says, briefly: what it is, and what it does at a high level.

The brief does NOT say: how it does it, why it does it, how it's consumed or who uses it, or its design and what it references.

Class declarations get Doxygen for template params.

## Inline detail

How something does what it does belongs inline, next to the code that does it — never in the header. Only the exact information needed to describe that step. Nothing more.

## TODOs

Keep them. They're useful. Doxygen style (`@todo`).

## Tests, benches, CMake

Same approach — terse, single-source, no cross-references — minus Doxygen.

# Docs

## Layout

`docs/` mirrors the source tree. A `README.md` goes in each directory that owns a concept, or a composed component with its own internal seam or nested sub-module (even when its public type is a class, not a concept). A leaf variation (a single concrete impl with no further nesting) gets a bullet in its parent's Variations, not its own README. Each module also gets a `DECISIONS.md`. No `STATUS.md` — milestones live in the `README.md`.

Exception: the `docs/` root has no own `README.md` in this sense. It is an index.

## The level rule

A doc describes only its own level: its sub-components, how they wire together, and its own variations. Nothing else.

Never reference upward (consumers), sideways (sibling components), or the specific implementations a level down.

No implementation detail. That lives in the code.

## Components vs variations

Decided by the relationship, not by whether it has its own README:

- A **variation** *is-a* this level's concept, a concrete implementation plugging into it. Listed under Variations. It may still have its own README when it is substantial or nests further; link to it, but it stays a variation here.
- A **component** is a sub-module this one *composes* that implements a different concept, a collaborator with its own seam. Listed under Components.

A module may have components, variations, both, or neither.

## README.md structure

In this order. Omit a section that does not apply:

1. What the component is. Brief.
2. ASCII diagram: how its parts fit into it. Names only this level's own concepts and seams, never a specific variation or its mechanism. Stops at the seam (the output boundary), never drawing the consumer beyond it.
3. Components: sub-modules nested below, and how they wire together. Terse.
4. Variations: one bullet each, name and what it does.
5. Milestones and sub-goals. A checklist. Strike through as achieved. Sub-goals are indented bullets under their milestone.

## DECISIONS.md

Numbered bullet list, one line per decision, stated in that module's terms. Numbering is module-local (`1…n`). No global decisions log — decisions live only in their module.

## Style

Markdown-formatting-happy but terse. Lines at a time, not paragraphs. Bullets over prose. Soft-wrap. No em dashes, no colons, no semicolons, no double-hyphens (`--`). Use a second sentence, or a bullet, instead.

## Documenting a module

Same procedure the first time and for any new module later.

1. Read the module's source. Document only what is there, only at this level.
2. Write `README.md` to the structure above.
3. Write `DECISIONS.md`: one line per decision, in this module's terms, no cross-module citations.
4. Delete the module's `STATUS.md` if present.

Only ever touch files in the module being documented.
