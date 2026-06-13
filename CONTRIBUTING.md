# Contributing to SacredSDK

Thank you for helping improve SacredSDK.

SacredSDK is a modding toolkit for Sacred Gold that allows mod authors to write Lua mods without modifying the original game installation. This repository currently contains:

* Documentation
* Python tooling
* Reverse-engineering notes
* Community research references

The DLL runtime implementation is distributed separately and is not currently part of the public repository.

---

# Before You Start

Please read:

1. `README.md`
2. `docs/MODDING_GUIDE.md`
3. `docs/README.md`

Most questions about the project structure, mod authoring workflow, and roadmap are already covered there.

---

# Cloning the Repository

```bash
git clone https://github.com/<owner>/SacredSDK.git
cd SacredSDK
```

Create a feature branch before making changes:

```bash
git checkout -b feature/my-change
```

---

# Repository Structure

```text
sdk/
├── README.md
├── docs/
│   ├── MODDING_GUIDE.md
│   ├── README.md
│   ├── 01..22-*.md
│   ├── community-refs.md
│   └── roadmap.md
└── tools/
    ├── README.md
    ├── *.py
    ├── hash_names.csv
    ├── smoke_test_proxy.bat
    └── ghidra/
        └── README.md
```

## docs/

Contains:

* User-facing modding documentation
* Reverse-engineering notes
* Historical research
* Community references
* Project roadmap

## tools/

Contains:

* FunkCode utilities
* Resource editing scripts
* Quest tooling
* Hash utilities
* Analysis helpers
* Ghidra automation scripts

---

# Contributing Documentation

Documentation contributions are highly encouraged.

Examples:

* Fixing inaccuracies
* Clarifying modding workflows
* Improving examples
* Adding missing explanations
* Expanding reverse-engineering findings
* Improving setup instructions

When updating documentation:

* Preserve historical findings and research notes.
* Clearly distinguish confirmed behavior from speculation.
* Prefer examples taken from actual Sacred content whenever possible.

---

# Contributing Python Tools

Most tooling in this repository is written in Python.

When modifying or adding tools:

* Follow existing coding conventions.
* Keep scripts focused on a single task.
* Document new command-line options.
* Update `tools/README.md` when behavior changes.

Include:

* Purpose of the tool
* Inputs
* Outputs
* Example usage

---

# Adding a New Mod Example

If you contribute a new example:

1. Place it in the appropriate example location.
2. Include comments explaining what it demonstrates.
3. Keep the example minimal and focused.
4. Update documentation references if necessary.

Examples should teach one concept clearly rather than demonstrate many features at once.

---

# Testing Changes Against Sacred

Testing is important because SacredSDK interacts with a closed-source game.

Before opening a PR:

## Documentation Changes

Verify:

* Instructions are accurate.
* Paths and filenames exist.
* Examples are valid.

## Tooling Changes

Run the affected tool against real project data when possible.

Verify:

* Output is generated correctly.
* Existing workflows are not broken.
* Generated data remains compatible with Sacred's formats.

## Modding Changes

When applicable:

1. Install the current SacredSDK runtime.
2. Place the modified content in the expected directory.
3. Launch Sacred Gold.
4. Verify behavior in-game.

Please include testing notes in your PR description.

---

# Reverse Engineering Contributions

Research contributions are welcome.

When documenting discoveries:

* Include evidence where possible.
* Reference offsets, resources, hashes, or scripts used.
* Clearly mark hypotheses and unverified findings.
* Avoid presenting assumptions as confirmed facts.

---

# Pull Request Expectations

A good pull request should:

* Solve one problem at a time.
* Be easy to review.
* Include documentation updates when necessary.
* Explain how the change was tested.

## PR Template

```markdown
## What does this PR do?

## Why?

## How was it tested?

## Notes for reviewers
```

---

# Review Guidelines

Reviewers typically look for:

* Technical correctness
* Consistency with existing documentation
* Reproducibility of findings
* Maintainability of tooling
* Accuracy of reverse-engineering notes

Feedback is part of the process. Revisions are normal.

---

# Issues

When opening an issue, include:

* Sacred Gold version
* Relevant files
* Reproduction steps
* Expected behavior
* Actual behavior
* Screenshots or logs when applicable

---

# Scope of Contributions

Currently preferred contributions include:

* Documentation improvements
* Python tooling improvements
* New examples
* Reverse-engineering research
* Community reference gathering
* Testing and verification

The DLL runtime implementation is not currently developed in this public repository.

---

Thank you for helping improve SacredSDK and the Sacred modding ecosystem.
