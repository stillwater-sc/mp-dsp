# Session log — 2026-10-06 — CMake 4 CI fix and repo rename

**Branch:** `main` (fix landed through `fix/ci-cmake4`, PR #210)
**Range:** `f9f9499..45eb8b9` (1 commit + 1 merge, after the triggering
commit `f9f9499`)
**Scope:** `.github/workflows/cmake.yml`, +11 / −0; plus local git
housekeeping

## Goal

`main` was red after `f9f9499` ("fix: cmake version update"). Get CI green,
merge, and tidy up.

## What happened

### 1. Diagnosis

`f9f9499` made two changes in `CMakeLists.txt`:

- `cmake_minimum_required(VERSION 3.22)` → `VERSION 4.0`
- Universal fallback fetch tag `v4.6.10` → `v5.1.0`

Three of the five CMake workflow jobs (Linux GCC, Linux Clang, RISC-V
cross-compile) failed at the **configure** step, before compiling anything:

```
CMake Error at CMakeLists.txt:1 (cmake_minimum_required):
  CMake 4.0 or higher is required.  You are running version 3.31.6
```

`ubuntu-latest` ships CMake 3.31.6. The Windows and macOS runners already
have CMake 4.x, so those jobs passed. The Universal bump was not involved.

### 2. Fix — PR #210 (`d1ca0ca`, merged `45eb8b9`)

We kept the 4.0 requirement as intended and provisioned CMake 4.x on the
Linux runners with `jwlawson/actions-setup-cmake@v2` (`cmake-version: '4.x'`):

- in the `build` matrix, gated on `runner.os == 'Linux'`;
- unconditionally in the `cross-riscv64` job.

All five platform jobs passed on the PR, along with CodeQL, CodeRabbit and
Snyk. Merged with a merge commit, and the remote branch was deleted.

### 3. Housekeeping

- Deleted the local `fix/ci-cmake4` branch and four older local branches
  already fully merged into `main`: `feature/104-sdr-demo`,
  `feature/105-sdr-docs`, `feature/209-8bit-study`,
  `fix/208-fractional-delay-default`. Pruned stale remote-tracking refs.
- **The repository was renamed** `mixed-precision-dsp` → `mp-dsp`. Pushes
  printed a "repository moved" notice, so `origin` now points to
  `git@github.sw:stillwater-sc/mp-dsp.git`. It keeps the `github.sw` SSH
  host alias, so the same key is still used. A fetch afterwards produced no
  notice.

## Design decisions worth remembering

- **Raising the CI toolchain was chosen over relaxing the requirement.**
  The alternative was `cmake_minimum_required(VERSION 3.22...4.0)`. That
  keeps CMake 4 policy behaviour wherever it is available, still configures
  on older CMake, and would need no CI change. It was not taken because the
  4.0 bump was a deliberate commit. It is still worth revisiting: a
  header-only library that requires CMake 4.0 asks every consumer to upgrade
  (see Follow-ups).

## State at wrap-up

- `main` at `45eb8b9`, in sync with `origin/main`. The only remaining
  branch, local or remote, is `main`.
- CI green on all five platforms.
- No release cut. The changes are recorded under `[Unreleased]` in
  `CHANGELOG.md`.

## Follow-ups

1. **Docs site base path is now wrong.** `docs-site/astro.config.*` has
   `base: '/mixed-precision-dsp'` and an edit-link `baseUrl` that uses the old
   repo name. GitHub reports the Pages URL as
   `https://stillwater-sc.github.io/mp-dsp/`, so on the next deploy assets and
   internal links will point at the wrong prefix. Hard-coded
   `/mixed-precision-dsp/...` links inside content pages (`signals.md`,
   `quantization.md`, `format-guide.md`, …) need the same update.
2. **Old repo name throughout the docs.** 28 files under `README.md`, `docs/`
   and `docs-site/src` still reference `mixed-precision-dsp`: the CI badge,
   clone instructions, the `FetchContent` `GIT_REPOSITORY`, and issue links.
   GitHub redirects most of these, but the badge and clone instructions
   should be corrected.
3. **Stale CMake minimum in the docs.** `README.md:179` and
   `docs/design.md:267` still say "CMake 3.22+".
4. **Reconsider `VERSION 3.22...4.0`.** This would restore support for
   consumers on distro CMake, such as Ubuntu 24.04's 3.28, unless something
   specifically needs CMake 4.
5. **`ubuntu-latest` moves to Ubuntu 26 on 2026-10-19**, per the runner
   notice. Watch the first CI run after that date.
6. `actions/upload-artifact@v5` targets Node 20, which is deprecated. The
   runner currently forces it onto Node 24, and a version bump will
   eventually be needed.
