# Omit Default Axis Sizes from CRDT Operations

## Problem

When creating columns/rows with default sizes, the CRDT operations include the size field unnecessarily:

```
O 1769967510272.0.83Weacis COL_SET mCIDLa29 {"pos":1,"size":100}
O 1769967510273.0.83Weacis ROW_SET R9zR53NN {"pos":1,"size":24}
```

Should be:
```
O 1769967510272.0.83Weacis COL_SET mCIDLa29 {"pos":1}
O 1769967510273.0.83Weacis ROW_SET R9zR53NN {"pos":1}
```

The size field should only be included when it differs from the default.

### Additional Issue

There's an inconsistency in default row height:
- `types.h`: `DEFAULT_ROW_HEIGHT = 24`
- `crdt_axis.cc` line 243: defaults to `21` when size field is missing

## Phase 1: Fix Default Row Height Inconsistency

- [ ] 1a: Update `crdt_axis.cc` to use `DEFAULT_ROW_HEIGHT` constant instead of hardcoded 21

## Phase 2: Omit Default Sizes in Operation Generation

- [ ] 2a: Update `luau_api.cc` to omit size field when creating columns/rows (lines 366-381)
- [ ] 2b: Update `fill_range.cc` to omit size field when creating columns/rows (lines 247-264)
- [ ] 2c: Update `crdt.cc` `bootstrapOpLog()` to only include size when non-default (lines 528-569)

## Phase 3: Add Tests

- [ ] 3a: Add unit tests verifying default sizes are omitted from operation payloads
- [ ] 3b: Add tests verifying non-default sizes are still included
