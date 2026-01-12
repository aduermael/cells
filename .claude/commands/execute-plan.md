# Execute or Continue plan

Please start or continue executing this plan: $1

## Pacing

- Only continue current phase if not finished or start next one
- Always take a break at the end of each phase or if something needs to be clarified
- It's also OK to take a break after any step if a lot was accomplished - no need to run all steps at once when the work is complex

## Commits

Create a commit for each step of each phase, including the plan update (checkmark at least) in the commit.

Commit message format: `<phase><step>: short description` (lowercase)
- Example: `1c: add validation for empty inputs`

## Plan updates

When checking off a step in the plan, optionally add 1-2 sentences explaining what was done if it's useful to extend the step description.

## Building

Use `bazel run :wasm-dist` to build the WASM distribution. This compiles the WASM module and TypeScript, then copies all files to `dist/wasm/`.

Do not use pnpm build commands directly - always use bazel build scripts.

## Tests

At the end of each phase, run tests in this order. Everything should pass:

1. `bazel run :test` (unit tests)
2. `bazel run :lint`
3. `bazel run :check-types`
4. `bazel run :wasm-dist` (build before E2E tests)
5. `bazel run :e2e` (E2E tests)
6. `bazel run :format`

For debugging E2E tests with a visible browser, use `bazel run :e2e-headed`.

## Phase completion

At the end of each phase, display:
- A quick summary of what was done
- ~3 lines about what comes next
