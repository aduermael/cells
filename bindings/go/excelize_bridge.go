package main

/*
#include <stdlib.h>
*/
import "C"
import (
	"github.com/xuri/excelize/v2"
)

// Version returns the excelize library version to verify integration works
//
//export ExcelizeVersion
func ExcelizeVersion() *C.char {
	// Use excelize to verify import works
	_ = excelize.NewFile()
	return C.CString("excelize-bridge-v0.1")
}

func main() {}
