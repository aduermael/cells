// Unit tests for EditingSession
// Tests core state management without browser dependencies

import assert from 'node:assert';

// Build the TS file first via esbuild (inline)
import { buildSync } from 'esbuild';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const srcPath = join(__dirname, '../../src/editing-session.ts');

// Build TypeScript to JavaScript in memory
const result = buildSync({
  entryPoints: [srcPath],
  bundle: true,
  format: 'esm',
  write: false,
  platform: 'node',
});

// Evaluate the built code
const code = result.outputFiles[0].text;
const module = await import(`data:text/javascript,${encodeURIComponent(code)}`);
const { EditingSession } = module;

// Test helpers
let testCount = 0;
let passCount = 0;

function test(name, fn) {
  testCount++;
  try {
    fn();
    passCount++;
    console.log(`  \u2713 ${name}`);
  } catch (e) {
    console.log(`  \u2717 ${name}`);
    console.log(`    ${e.message}`);
  }
}

function assertEqual(actual, expected, msg = '') {
  if (JSON.stringify(actual) !== JSON.stringify(expected)) {
    throw new Error(`${msg}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

function assertTrue(value, msg = '') {
  if (!value) {
    throw new Error(`${msg}: expected truthy value`);
  }
}

function assertFalse(value, msg = '') {
  if (value) {
    throw new Error(`${msg}: expected falsy value`);
  }
}

// Tests
console.log('\nEditingSession Unit Tests\n');

console.log('Session Lifecycle:');

test('Session is not active initially', () => {
  const session = new EditingSession();
  assertFalse(session.isActive(), 'Should not be active');
  assertEqual(session.getState(), null, 'State should be null');
});

test('start() creates active session', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hello');
  assertTrue(session.isActive(), 'Should be active');
  const state = session.getState();
  assertEqual(state.sheetId, 'sheet1', 'sheetId');
  assertEqual(state.col, 0, 'col');
  assertEqual(state.row, 0, 'row');
  assertEqual(state.value, 'Hello', 'value');
});

test('start() sets cursor to end of value', () => {
  const session = new EditingSession();
  session.start('sheet1', 2, 3, 'Test');
  const state = session.getState();
  assertEqual(state.cursorStart, 4, 'cursorStart at end');
  assertEqual(state.cursorEnd, 4, 'cursorEnd at end');
});

test('start() defaults activeEditor to "cell"', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Test');
  assertEqual(session.getActiveEditor(), 'cell', 'Default is cell');
  assertEqual(session.getState().activeEditor, 'cell', 'State has activeEditor');
});

test('start() accepts activeEditor parameter', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Test', 'formula');
  assertEqual(session.getActiveEditor(), 'formula', 'Set to formula');
});

test('getActiveEditor() returns "cell" when no session', () => {
  const session = new EditingSession();
  assertEqual(session.getActiveEditor(), 'cell', 'Default when no session');
});

test('clear() resets session', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hello');
  session.clear();
  assertFalse(session.isActive(), 'Should not be active');
  assertEqual(session.getState(), null, 'State should be null');
});

console.log('\nCursor Management:');

test('setCursor() updates both start and end', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hello');
  session.setCursor(2);
  const sel = session.getSelection();
  assertEqual(sel.start, 2, 'start');
  assertEqual(sel.end, 2, 'end');
});

test('setCursor() with explicit end creates selection', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hello');
  session.setCursor(1, 4);
  const sel = session.getSelection();
  assertEqual(sel.start, 1, 'start');
  assertEqual(sel.end, 4, 'end');
});

test('setCursor() clamps to value length', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hi');
  session.setCursor(10, 20);
  const sel = session.getSelection();
  assertEqual(sel.start, 2, 'start clamped');
  assertEqual(sel.end, 2, 'end clamped');
});

test('setCursor() clamps negative to zero', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hi');
  session.setCursor(-5);
  const sel = session.getSelection();
  assertEqual(sel.start, 0, 'start clamped to 0');
  assertEqual(sel.end, 0, 'end clamped to 0');
});

test('getSelection() returns {0,0} when no session', () => {
  const session = new EditingSession();
  const sel = session.getSelection();
  assertEqual(sel, { start: 0, end: 0 }, 'Default selection');
});

console.log('\nValue Management:');

test('getValue() returns empty string when no session', () => {
  const session = new EditingSession();
  assertEqual(session.getValue(), '', 'Empty string');
});

test('getValue() returns current value', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Test');
  assertEqual(session.getValue(), 'Test', 'Returns value');
});

test('setValue() updates value', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Old');
  session.setValue('New');
  assertEqual(session.getValue(), 'New', 'Updated value');
});

test('setValue() preserves cursor when within range', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hello');
  session.setCursor(2);
  session.setValue('World');
  const sel = session.getSelection();
  assertEqual(sel.start, 2, 'Cursor preserved');
});

test('setValue() clamps cursor when value shrinks', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hello');
  session.setCursor(5);
  session.setValue('Hi');
  const sel = session.getSelection();
  assertEqual(sel.start, 2, 'Cursor clamped');
  assertEqual(sel.end, 2, 'End clamped');
});

console.log('\nText Manipulation:');

test('insertAt() inserts at start', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'World');
  const newPos = session.insertAt(0, 'Hello ');
  assertEqual(session.getValue(), 'Hello World', 'Value updated');
  assertEqual(newPos, 6, 'Returns new cursor position');
  assertEqual(session.getSelection(), { start: 6, end: 6 }, 'Cursor after insert');
});

test('insertAt() inserts at middle', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'HelloWorld');
  const newPos = session.insertAt(5, ' ');
  assertEqual(session.getValue(), 'Hello World', 'Value updated');
  assertEqual(newPos, 6, 'Returns new cursor position');
});

test('insertAt() inserts at end', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hello');
  const newPos = session.insertAt(5, ' World');
  assertEqual(session.getValue(), 'Hello World', 'Value updated');
  assertEqual(newPos, 11, 'Returns new cursor position');
});

test('insertAt() clamps position to valid range', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Test');
  session.insertAt(100, '!');
  assertEqual(session.getValue(), 'Test!', 'Inserted at end');
});

test('insertAt() returns 0 when no session', () => {
  const session = new EditingSession();
  const pos = session.insertAt(0, 'text');
  assertEqual(pos, 0, 'Returns 0');
});

test('replaceRange() replaces text', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hello World');
  const newPos = session.replaceRange(6, 11, 'Universe');
  assertEqual(session.getValue(), 'Hello Universe', 'Value updated');
  assertEqual(newPos, 14, 'Returns new cursor position');
  assertEqual(session.getSelection(), { start: 14, end: 14 }, 'Cursor after replace');
});

test('replaceRange() can delete by replacing with empty', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hello World');
  session.replaceRange(5, 11, '');
  assertEqual(session.getValue(), 'Hello', 'Text deleted');
});

test('replaceRange() handles reversed range', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hello');
  // Start > end should still work (clamped)
  session.replaceRange(3, 1, 'X');
  // clampedStart = 3, clampedEnd = max(3, 1) = 3, so inserts at 3
  assertEqual(session.getValue(), 'HelXlo', 'Handles edge case');
});

test('insertAtCursor() inserts at cursor', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'HelloWorld');
  session.setCursor(5);
  const newPos = session.insertAtCursor(' ');
  assertEqual(session.getValue(), 'Hello World', 'Value updated');
  assertEqual(newPos, 6, 'Returns new cursor position');
});

test('insertAtCursor() replaces selection', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Hello World');
  session.setCursor(6, 11); // Select "World"
  const newPos = session.insertAtCursor('Universe');
  assertEqual(session.getValue(), 'Hello Universe', 'Selection replaced');
  assertEqual(newPos, 14, 'Cursor at end of insert');
});

console.log('\nEvent Subscription:');

test('subscribe() receives state changes', () => {
  const session = new EditingSession();
  const events = [];
  session.subscribe((state) => events.push(state));

  session.start('sheet1', 0, 0, 'Test');
  assertEqual(events.length, 1, 'One event on start');
  assertTrue(events[0] !== null, 'State not null');

  session.setValue('New');
  assertEqual(events.length, 2, 'Event on setValue');

  session.clear();
  assertEqual(events.length, 3, 'Event on clear');
  assertEqual(events[2], null, 'Null state on clear');
});

test('unsubscribe stops notifications', () => {
  const session = new EditingSession();
  const events = [];
  const unsub = session.subscribe((state) => events.push(state));

  session.start('sheet1', 0, 0, 'Test');
  assertEqual(events.length, 1, 'One event');

  unsub();

  session.setValue('New');
  assertEqual(events.length, 1, 'No more events after unsub');
});

test('getState() returns copy (immutable)', () => {
  const session = new EditingSession();
  session.start('sheet1', 0, 0, 'Test');

  const state1 = session.getState();
  const state2 = session.getState();

  assertTrue(state1 !== state2, 'Different objects');
  assertEqual(state1.value, state2.value, 'Same content');
});

// Summary
console.log('\n' + '='.repeat(40));
console.log(`Tests: ${passCount}/${testCount} passed`);
if (passCount === testCount) {
  console.log('All tests passed!\n');
  process.exit(0);
} else {
  console.log(`${testCount - passCount} tests failed\n`);
  process.exit(1);
}
