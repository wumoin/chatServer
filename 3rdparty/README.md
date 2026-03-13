# 3rdparty

This directory contains vendored third-party dependencies used by `chatServer`.

Current dependencies:

- `drogon/`: Drogon `v1.9.12` source snapshot from the official repository
  `https://github.com/drogonframework/drogon`
- `drogon/trantor/`: Trantor pinned to Drogon submodule commit
  `5000e2a72687232c8675b28ce86a29ed7d44309e`
- `hiredis/`: hiredis `v1.2.0` source snapshot from the official repository
  `https://github.com/redis/hiredis`
- `jsoncpp/`: JsonCpp `1.9.6` source snapshot from the official repository
  `https://github.com/open-source-parsers/jsoncpp`

Rules for this folder:

- Keep third-party code under `chatServer/3rdparty/`.
- Pin versions explicitly instead of tracking moving branches.
- Build third-party code out-of-source under `tmpbuild/chatServer/3rdparty/`.
- Prune non-build-essential content when vendoring, such as examples, tests,
  CI metadata, local `.git/` metadata, and developer-only helper files, as
  long as the current `chatServer` build remains reproducible.
