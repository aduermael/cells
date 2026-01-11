#!/usr/bin/env python3
"""Generate a large test .zcd file with multiple sheets, values, and formulas."""

import random
import string

def generate_id():
    """Generate an 8-character base62 ID."""
    chars = string.ascii_letters + string.digits
    return ''.join(random.choice(chars) for _ in range(8))

def generate_sheet(name, num_rows, num_cols, num_formulas):
    """Generate a sheet with rows, columns, and cells."""
    sheet_id = generate_id()
    col_ids = [generate_id() for _ in range(num_cols)]
    row_ids = [generate_id() for _ in range(num_rows)]

    lines = []
    lines.append(f'S {sheet_id} "{name}"')
    lines.append('')

    # Columns with positions
    lines.append('#cols')
    for i, col_id in enumerate(col_ids):
        lines.append(f'C {col_id} {i}')
    lines.append('')

    # Rows with positions
    lines.append('#rows')
    for i, row_id in enumerate(row_ids):
        lines.append(f'R {row_id} {i}')
    lines.append('')

    # Pre-select which cells will be formulas (excluding first row/col)
    # to guarantee exact count
    formula_positions = set()
    valid_positions = [(r, c) for r in range(1, num_rows) for c in range(1, num_cols)]
    random.shuffle(valid_positions)
    for i in range(min(num_formulas, len(valid_positions))):
        formula_positions.add(valid_positions[i])

    # Cells
    lines.append('#cells')
    formula_count = 0
    value_count = 0

    sample_strings = ['Alpha', 'Beta', 'Gamma', 'Delta', 'Epsilon',
                      'Data', 'Test', 'Sample', 'Value', 'Result']

    for row_idx, row_id in enumerate(row_ids):
        for col_idx, col_id in enumerate(col_ids):
            cell_id = generate_id()

            if (row_idx, col_idx) in formula_positions:
                # Create a formula referencing a previous cell
                ref_col = col_ids[col_idx - 1]
                ref_row = row_ids[row_idx - 1]
                formula = f'=${ref_col}${ref_row}*2'
                lines.append(f'X {cell_id} {col_id} {row_id} f "{formula}"')
                formula_count += 1
            elif random.random() < 0.2:  # 20% chance of string
                text = f'{random.choice(sample_strings)}{random.randint(1, 999)}'
                lines.append(f'X {cell_id} {col_id} {row_id} s "{text}"')
                value_count += 1
            else:  # Number
                value = round(random.uniform(-1000, 10000), 2)
                lines.append(f'X {cell_id} {col_id} {row_id} n {value}')
                value_count += 1

    return lines, formula_count, value_count

def main():
    random.seed(42)  # For reproducibility

    doc_id = generate_id()

    output = []
    output.append('#cells v1')
    output.append(f'D {doc_id} "Large Dataset"')
    output.append('')

    total_values = 0
    total_formulas = 0

    # Sheet 1: Data - 100 rows x 50 columns, 200 formulas
    sheet_lines, formulas, values = generate_sheet('Data', 100, 50, 200)
    output.extend(sheet_lines)
    output.append('')
    total_values += values
    total_formulas += formulas
    print(f'  Data: {values} values, {formulas} formulas')

    # Sheet 2: Summary - 80 rows x 20 columns, 150 formulas
    sheet_lines, formulas, values = generate_sheet('Summary', 80, 20, 150)
    output.extend(sheet_lines)
    output.append('')
    total_values += values
    total_formulas += formulas
    print(f'  Summary: {values} values, {formulas} formulas')

    # Sheet 3: Analysis - 60 rows x 30 columns, 100 formulas
    sheet_lines, formulas, values = generate_sheet('Analysis', 60, 30, 100)
    output.extend(sheet_lines)
    total_values += values
    total_formulas += formulas
    print(f'  Analysis: {values} values, {formulas} formulas')

    # Write to file
    with open('testdata/large.zcd', 'w') as f:
        f.write('\n'.join(output))
        f.write('\n')

    print(f'Generated large.zcd:')
    print(f'  - 3 sheets, {total_values} values, {total_formulas} formulas')

if __name__ == '__main__':
    main()
