# CI and component releases

GitHub Actions uses `ubuntu-24.04` for pull requests, pushes to `main`, and version tags (`v*`). Tests and packaging must pass before a tag release is published. Ordinary branch and PR builds only upload temporary Actions artifacts.

For an existing tag that predates the workflow, select **Actions → CI and Release → Run workflow**, use the **main** branch and enter the exact tag. The workflow checks out that tag separately from the current CI implementation. Existing tags are never moved. Already published releases are not overwritten; interrupted draft releases can be retried.

The `AbilityFramework` release includes build outputs, `release.json` (source SHA, tag, CI recipe SHA, platform and run link), and `SHA256SUMS`. Native executables must pass static x86_64 ELF checks. Web assets need same-origin `/api` and `/ws` reverse proxies. Python distribution versions remain those declared by the tagged source, even when the Git tag includes a maintenance suffix.

Build commands live in `.github/scripts/build.sh` and follow quick-start's component build and static-linkage rules. Actions are pinned to full commit SHAs. Component validation does not run GPU, real hardware, external-model or full-stack acceptance tests. Use quick-start's manifest to select a compatible set of component releases.

中文：PR/main 执行组件验证，推送版本 Tag 后自动构建发布；已有 Tag 可在 main 上手动填写 Tag 补发。构建源码与 CI 脚本分别检出并记录 SHA，发布前检查产物校验和，不覆盖已发布版本。本流水线不代表整栈、GPU 或真机验收通过。
