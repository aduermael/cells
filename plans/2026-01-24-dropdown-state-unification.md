# Plan: Dropdown State Unification & Shared Container Component

This plan addresses two requirements:

1. **Single dropdown at a time**: Implement solid state control to prevent multiple dropdowns from being open simultaneously
2. **Unified container component**: Ensure all dropdowns use a shared frame/container for consistent styling (borders, shadows, padding)

## Current State Analysis

### Existing Infrastructure

**MenuStateManager** (`menu-state.ts`):
- Singleton that ensures only one menu is open at a time
- Already handles: `export`, `collaborate`, `context`, `bgColor`, `textColor`, `fontFamily`, `fontSize`, `format`, `currency`, `customFormat`, `merge`, `border`
- Pattern: Components register with a close callback, call `openMenu(id)` to open (auto-closes others)

**Partial Integration** (from `2026-01-20-unified-menu-state.md`):
- `style-controls.ts`: Already integrated with MenuStateManager
- `format-controls.ts`: NOT integrated (phases 3a-3c incomplete)
- `merge-controls.ts`: NOT integrated (phases 4a-4c incomplete)
- `border-controls.ts`: Already integrated per commit history

**Shared Styling** (`styles.css`):
- `.dropdown-menu` class exists with standard styling (border, shadow, border-radius, padding, z-index)
- Some components create custom menu elements that don't use this class

### Components Requiring Work

| Component | MenuStateManager | Uses .dropdown-menu |
|-----------|------------------|---------------------|
| `style-controls.ts` (colors, fonts) | ✅ | Mixed |
| `format-controls.ts` | ❌ | ✅ |
| `merge-controls.ts` | ❌ | Custom styles |
| `border-controls.ts` | ✅ | ✅ |
| `named-ranges-dropdown.ts` | ❌ | Custom styles |
| `context-menu.ts` | ✅ | Uses `.context-menu` |
| `formula-autocomplete.ts` | ❌ | Custom styles |
| `script-autocomplete.ts` | ❌ | Custom styles |

---

## Phase 1: Create Shared DropdownFrame Component

Create a reusable TypeScript class that provides consistent dropdown container behavior.

- [x] 1a: Create `dropdown-frame.ts` with `DropdownFrame` class
  - Constructor accepts: anchor element, content element, menu ID (for MenuStateManager)
  - Methods: `open()`, `close()`, `toggle()`, `isOpen()`, `destroy()`
  - Automatically registers with MenuStateManager
  - Applies consistent CSS class `.dropdown-frame` to content wrapper
  - Handles outside click and Escape key to close
  - Uses `positionDropdown()` from `dropdown-utils.ts` for positioning

- [x] 1b: Add `.dropdown-frame` CSS class to `styles.css`
  - Use existing `.dropdown-menu` styling as base (same border, shadow, border-radius, padding, z-index)
  - Uses fixed positioning (positionDropdown() handles placement)
  - Added `.dropdown-frame.hidden` for hide/show toggling

- [x] 1c: Add E2E test for DropdownFrame behavior
  - Created `dropdown-frame.test.mjs` with tests using existing toolbar dropdowns (font family, font size, color pickers, border)
  - Tests: opening one closes others, outside click closes, Escape key closes, cross-component mutual exclusivity

## Phase 2: Integrate FormatControls with MenuStateManager

Complete the partial integration from the previous plan.

- [x] 2a: Import and register `format`, `currency`, `customFormat` menus in FormatControls constructor
- [x] 2b: Update `openDropdown()` / `toggleDropdown()` to call `menuState.openMenu('format')`
- [x] 2c: Update `openCurrencyDropdown()` to call `menuState.openMenu('currency')`
- [x] 2d: Update `openCustomFormatPanel()` to call `menuState.openMenu('customFormat')`
- [x] 2e: Update close methods to call corresponding `menuState.closeMenu()` and added Escape key handler to close dropdowns and notify MenuStateManager

## Phase 3: Integrate MergeControls with MenuStateManager

- [ ] 3a: Import MenuStateManager and register `merge` menu in MergeControls constructor
- [ ] 3b: Update `toggleDropdown()` to call `menuState.openMenu('merge')` when opening
- [ ] 3c: Update `closeDropdown()` to call `menuState.closeMenu('merge')`
- [ ] 3d: Apply `.dropdown-menu` class to merge dropdown menu for consistent styling

## Phase 4: Integrate NamedRangesDropdown with MenuStateManager

- [ ] 4a: Add `namedRanges` to `MenuId` type in `menu-state.ts`
- [ ] 4b: Import MenuStateManager and register in NamedRangesDropdown
- [ ] 4c: Update show/hide methods to use MenuStateManager
- [ ] 4d: Apply `.dropdown-frame` class for consistent styling

## Phase 5: Integrate Autocomplete Popups with MenuStateManager

- [ ] 5a: Add `formulaAutocomplete` and `scriptAutocomplete` to `MenuId` type
- [ ] 5b: Register formula-autocomplete with MenuStateManager
- [ ] 5c: Register script-autocomplete with MenuStateManager
- [ ] 5d: Apply `.dropdown-frame` class to autocomplete popups

## Phase 6: Add Comprehensive E2E Tests

- [ ] 6a: Test that opening format dropdown closes style controls dropdowns
- [ ] 6b: Test that opening merge dropdown closes format dropdown
- [ ] 6c: Test that opening context menu closes all toolbar dropdowns
- [ ] 6d: Test that opening named ranges dropdown closes other dropdowns
- [ ] 6e: Test autocomplete closes when toolbar dropdown opens

---

## Architecture Notes

### MenuStateManager Flow

```
User clicks dropdown button
       ↓
Component calls menuState.openMenu('myMenu')
       ↓
MenuStateManager iterates all registered menus
       ↓
For each menu (except 'myMenu'), calls its close callback
       ↓
Component opens its dropdown (CSS class + position)
```

### DropdownFrame Usage Pattern

```typescript
// In component constructor
this.dropdownFrame = new DropdownFrame({
  anchor: this.dropdownBtn,
  content: this.dropdownMenu,
  menuId: 'myMenu',
  onOpen: () => this.onDropdownOpen(),
  onClose: () => this.onDropdownClose(),
});

// To open
this.dropdownFrame.open();

// To close
this.dropdownFrame.close();

// To toggle
this.dropdownFrame.toggle();
```

### CSS Class Hierarchy

```
.dropdown-frame          // Base frame: shadow, border, border-radius, z-index
├── .dropdown-menu       // Menu-style: vertical list of items
├── .color-picker-popup  // Color picker specific overrides
└── .autocomplete-popup  // Autocomplete specific overrides
```
