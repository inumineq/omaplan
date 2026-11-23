# Rules for agents in Omaplan

**Language: everything in this repository is in English.** All documentation, code, comments, identifiers,
commit messages, CLI help, MCP descriptions and the skill are in English. UI strings are English source strings in
`qsTr()`/`tr()`; translations live in `i18n/*.ts` (Qt Linguist files, compiled into the binary; see
`docs/translating.md`). User input may be in any language.

**This is a public repository. No personal data may enter it** – not in code, tests, fixtures, documentation,
screenshots, commit messages, branch names or pull requests:

- no names of real people, organisations or workplaces; no email addresses
- no host names, IP addresses, tailnet names or home directories with a user name (write `~/…`)
- no real tasks, calendar data or AI answers to real data; use invented examples ("Blog", "Client A")
- screenshots only with invented data and the default accent
- commits with the author `inumineq <6770514+inumineq@users.noreply.github.com>`

Check `git diff --cached` for the above before every commit. When in doubt, keep it out of the repository and put it
in `~/claude/omaplan/` on the machine instead.

See also `SPEC.md`, `docs/architecture.md` and `docs/adr/`.
