# Unified Menu State Management

## Problem

Currently, toolbar menus can be opened simultaneously:
- Background color, text color, merge cells, borders, font family, font size, format dropdown, currency dropdown, and custom format panel all manage their own open/close state independently
- The existing `MenuStateManager` only coordinates Export, Collaborate, and Context menus
- This leads to a confusing UX where multiple dropdowns/popups can be visible at once

## Solution

Extend the existing `MenuStateManager` to handle all toolbar menus, ensuring only one menu can be open at a time across the entire application.

---

## Phase 1: Extend MenuStateManager with toolbar menu IDs
- [ ] 1a: Add new menu IDs to the `MenuId` type for all toolbar menus: `bgColor`, `textColor`, `fontFamily`, `fontSize`, `format`, `currency`, `customFormat`, `merge`, `border`

## Phase 2: Integrate StyleControls with MenuStateManager
- [ ] 2a: Register `bgColor` and `textColor` menus in StyleControls constructor
- [ ] 2b: Update `toggleColorPopup()` to call `menuState.openMenu()` instead of just toggling CSS class
- [ ] 2c: Update `closeColorPopups()` to call `menuState.closeMenu()`
- [ ] 2d: Register `fontFamily` and `fontSize` menus
- [ ] 2e: Update `toggleFontDropdown()` to use MenuStateManager
- [ ] 2f: Update `closeFontDropdowns()` to use MenuStateManager

## Phase 3: Integrate FormatControls with MenuStateManager
- [ ] 3a: Register `format`, `currency`, and `customFormat` menus in FormatControls constructor
- [ ] 3b: Update dropdown toggle methods to use MenuStateManager
- [ ] 3c: Update close methods to use MenuStateManager

## Phase 4: Integrate MergeControls with MenuStateManager
- [ ] 4a: Register `merge` menu in MergeControls constructor
- [ ] 4b: Update `toggleDropdown()` to use MenuStateManager
- [ ] 4c: Update `closeDropdown()` to use MenuStateManager

## Phase 5: Integrate BorderControls with MenuStateManager
- [ ] 5a: Register `border` menu in BorderControls constructor
- [ ] 5b: Update `toggleDropdown()` to use MenuStateManager
- [ ] 5c: Update `closeDropdown()` to use MenuStateManager

## Phase 6: Clean up redundant document click handlers
- [ ] 6a: Remove individual document click listeners from each control that are now redundant (the MenuStateManager callbacks handle closing)
