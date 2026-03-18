# Ambiguity Report

1. The official repository exposes a public `master` branch.
2. The GitHub releases page appears stale, but newer official tags such as `v4.1.5` are referenced publicly. The stable channel is therefore pinned to `v4.1.5`, and dev tracks `master`.
3. The benchmark-mode/stdout ambiguity is solved exactly as in the libjpeg package by running metrics and payload collection in two separate subprocess invocations.
