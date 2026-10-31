# ALI Index Format Versions

## Segment format (ALIS)

| Version | Status  | Description                                      |
|---------|---------|--------------------------------------------------|
| 1       | Current | Initial versioned binary segment format          |

### v1 layout

Each segment is a directory containing:

| File                   | Contents                                              |
|------------------------|-------------------------------------------------------|
| `segment_header.bin`   | Magic `ALIS`, format version, generation, counts      |
| `terms.bin`            | Sorted term dictionary with posting offsets           |
| `postings.bin`         | Compressed posting-list blocks (VByte + delta)        |
| `blocks.bin`           | `BlockMeta` array for Block-Max WAND                  |
| `docs.bin`             | Document table: doc_id, term_count, URL, title, excerpt |
| `content.bin`          | Optional Tier-3 full-text blobs (not in v0.1)         |

### Codec identifier byte (stored in segment_header.bin)

| ID   | Codec  | Status        |
|------|--------|---------------|
| 0x01 | VByte  | Current default |
| 0x02 | SIMDBp128 | Planned    |
| 0x03 | EliasFano | Planned   |

---

## Manifest format (ALIM)

| Version | Status  | Description                              |
|---------|---------|------------------------------------------|
| 1       | Current | Binary manifest with atomic-rename update |

Manifest updates follow: write to `manifest.bin.tmp` → fsync → rename.
A `manifest.bin.tmp` without a corresponding `manifest.bin` is a
crash artifact and must be ignored on open.

---

## Compatibility rules

- A reader **must** reject segments with an unknown format version.
- A reader **must** reject segments whose magic bytes do not match.
- Adding new fields at the end of existing records is a minor version change.
- Changing existing field offsets or semantics requires a new major version.
- Both reader and writer must record the format version in every file header.
