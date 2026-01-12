# Create a plan

## Planning System

1. Plans should be created at `plans/<date>-<plan-name>.md` (e.g., `2026-01-15-feature-name.md`)
2. **Use this format:**

	- Clear phases (numbered: 1, 2, 3...)
	- Subtasks within each phase as checkmarks: `- [ ]` (pending) or `- [x]` (done)

	**Important:** ALL phases must use checkmarks for their tasks. Each checkmark should correspond to a single commit. The checkmark should be marked `- [x]` in the same commit that completes the task. Phase tasks should be prefixed with parent phase number and letters (a, b, c, etc.)
	
	Example:
	
	```markdown
	## Phase 1: One Phase
	- [x] 1a: Some task
	- [x] 1b: Another task
	- [ ] 1c: One more task
	
	## Phase 2: Another Phase
	- [ ] 2a: Some task
	- [ ] 2b: Another task
	- [ ] 2c: One more task
	```

3. Check previous plans in `plans/` and recent commits when creating a plan to see if it builds on previous work.

4. In some cases, phases or tasks can be done in parallel using parallel agents. Only use this notation when parallelizing is clearly beneficial:

	```markdown
	## Phase 1: One Phase
	- [x] 1a: Some task
	- [x] 1b: Another task
	- [ ] 1c: One more task

	**Parallel Phases: 2,3**

	## Phase 2: Another Phase
	- [ ] 2a: Some task

	**Parallel Tasks: 2b, 2c**

	- [ ] 2b: Another task
	- [ ] 2c: One more task

	## Phase 3: One More Phase
	- [ ] 3a: Some task
	- [ ] 3b: Another task
	- [ ] 3c: One more task
	```

	In this example, phases 2 and 3 can run in parallel. Within phase 2, tasks 2b and 2c can also run in parallel. Add blank lines around annotations for proper markdown rendering.

## Design & Execution

1. Don't include information about how the plan will be executed (commits, tests, etc.). This is described in the `execute-plan` command.

2. Simply commit the plan when done with summary of what it contains.

3. Ask clarifying questions if needed before or after committing. Expect follow-up prompts to refine the plan. Commit each plan update.

4. Print that `/execute-plan plans/<date>-<plan-name>.md` should be used to execute it.

## New plan

Create a new plan to address this:

$ARGUMENTS
