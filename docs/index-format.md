# ALI Index Format Specification

See `format/FORMAT_VERSION.md` for the version table and compatibility rules.

## Segment directory contents

```
seg_<id>/
├── segment_header.bin    # Validation header
├── terms.bin             # Sorted term dictionary
├── postings.bin          # Compressed posting-list blocks
├── blocks.bin            # BlockMeta (Block-Max WAND bounds)
└── docs.bin              # Document table (URL, title, excerpt)
```

## Encoding

- Document IDs are stored as sorted delta-encoded gaps.
- Default codec: VByte (codec ID `0x01`).
- Future codecs (SIMDBp128, Elias-Fano) will be selected by benchmark.

## Invariants

- Segment files are **immutable** after publication.
- Publication is atomic: write to tmp → fsync → rename.
- A partially written segment is never visible to readers.
- Every file starts with a magic byte sequence and format version.
- Checksums are planned for v1.1 (not yet in initial format).
