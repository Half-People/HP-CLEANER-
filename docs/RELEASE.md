# Release guide (maintainers)

**English** | [繁體中文](RELEASE.zh-TW.md)

Checklist for publishing **HP CLEANER++** on GitHub Releases.

---

## Before tagging

- [ ] Bump `project(VERSION …)` in [`CMakeLists.txt`](../CMakeLists.txt)
- [ ] Update [`CHANGELOG.md`](../CHANGELOG.md)
- [ ] Update version in [`docs/release-notes/`](../docs/release-notes/) if needed
- [ ] Build Release: `cmake --build build --config Release`
- [ ] Smoke test: launch, language switch, cleanup scan, disk health

## Package

From the `Github/` directory:

```powershell
powershell -ExecutionPolicy Bypass -File tools\package-release.ps1
```

Output: `dist/HP_CLEANER++-{version}-win64.zip` and `.sha256`.

The zip includes:

- `HP_CLEANER++.exe`
- `i18n/` (language packs)
- `INSTALL.md` + `INSTALL.zh-TW.md`

## GitHub Release

1. Tag: `v{version}` on `main` (e.g. `v1.0.0`)
2. Title: `HP CLEANER++ v{version} — {short summary}`
3. Paste body from [`docs/release-notes/v{version}.md`](release-notes/v1.0.0.md) (bilingual EN + zh-TW)
4. Attach `HP_CLEANER++-{version}-win64.zip` and `.sha256`
5. Mark as **Latest** if this is the current stable release

## Commit without Cursor co-author

When committing from Cursor Agent, use:

```powershell
$env:GIT_AUTHOR_NAME="Half-People"
$env:GIT_AUTHOR_EMAIL="halfpeople62336180@gmail.com"
$env:GIT_COMMITTER_NAME="Half-People"
$env:GIT_COMMITTER_EMAIL="halfpeople62336180@gmail.com"
git add -A
$tree = (git write-tree).Trim()
$parent = (git rev-parse HEAD).Trim()
$commit = (git commit-tree $tree -p $parent -m "Your message").Trim()
git update-ref refs/heads/main $commit
git push origin main
```

Also disable **Cursor Settings → Agents → Attribution → Commit Attribution**.
