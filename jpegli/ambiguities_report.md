# Ambiguity Report

1. The official `google/jpegli` repository exposes a public `main` branch but no published releases.
2. Consequently, both stable and dev channels are mapped to `main`, exactly per the previously accepted ambiguity-resolution rule.
3. The benchmark-mode/stdout ambiguity is solved exactly as in the libjpeg package by running metrics and payload collection in two separate subprocess invocations.
