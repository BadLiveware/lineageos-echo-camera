# LineageOS 18.1 camera patch bundle

The bundle is split by ownership:

- `shared/` contains payloads referenced by both devices;
- `checkers/` contains Checkers-only payloads and its ordered `series.tsv`;
- `crown/` contains Crown-only payloads and its ordered `series.tsv`.

`SHA256SUMS` covers every patch payload across all three directories.

Verify payload integrity:

```sh
sha256sum -c SHA256SUMS
```

Validate a series against a matching Android checkout:

```sh
../../scripts/validate-patch-bundle.sh checkers /path/to/lineage-18.1
../../scripts/validate-patch-bundle.sh crown /path/to/lineage-18.1
```

Regenerate one device series from a development checkout:

```sh
../../scripts/regenerate-patches.sh checkers /path/to/lineage-18.1
../../scripts/regenerate-patches.sh crown /path/to/lineage-18.1
```

The `scope` column assigns source paths to each generated payload. Shared patches are therefore regenerated from the same owned paths regardless of which device series invokes regeneration.

Proprietary binaries are intentionally excluded. Regenerate the selected device's vendor tree from an authorized firmware source.
