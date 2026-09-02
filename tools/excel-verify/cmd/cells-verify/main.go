// cells-verify drives Excel (Windows COM) to produce golden xlsx files.
//
//	cells-verify excel-save <input.xlsx> <output.xlsx>
//	cells-verify version
package main

import (
	"fmt"
	"os"

	"cells-verify/internal/excel"
)

const toolVersion = "0.1.0"

func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintf(os.Stderr, "cells-verify: %v\n", err)
		os.Exit(1)
	}
}

func run(args []string) error {
	if len(args) == 0 || args[0] == "-h" || args[0] == "--help" {
		printUsage()
		return nil
	}

	switch args[0] {
	case "excel-save":
		if len(args) != 3 {
			return fmt.Errorf("usage: cells-verify excel-save <input.xlsx> <output.xlsx>")
		}
		return excel.NewHost().OpenSave(args[1], args[2])
	case "version":
		fmt.Printf("cells-verify %s\n", toolVersion)
		return nil
	default:
		return fmt.Errorf("unknown command %q\n\n%s", args[0], usage)
	}
}

const usage = `cells-verify — generate Excel goldens (Windows COM)

Commands:
  excel-save <input.xlsx> <output.xlsx>
      Open input in Excel and Save As xlsx to output.
      Requires Windows + Microsoft Excel. Off Windows this command errors.

  version
      Print cells-verify version.

  -h, --help
      Show this help.

Goldens must be generated on Windows. macOS Excel is not used.
`

func printUsage() {
	fmt.Fprint(os.Stdout, usage)
}
