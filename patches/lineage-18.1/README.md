# LineageOS 18.1 patch series

Apply these patches in the order listed by `series.tsv`. Each row records the Android project path, upstream repository, branch, exact base commit, and patch filename.

Use the repository scripts rather than applying files individually:

```bash
./scripts/apply-patches.sh --check /path/to/lineage-18.1
./scripts/apply-patches.sh /path/to/lineage-18.1
```

`SHA256SUMS` covers the patch payloads. Proprietary vendor binaries are intentionally excluded; regenerate `vendor/amazon/checkers` with the patched device tree's extraction script.
