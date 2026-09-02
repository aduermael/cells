// Package golden reads and writes Excel golden sidecar metadata.
package golden

import (
	"encoding/json"
	"fmt"
	"os"
	"time"
)

const toolName = "cells-verify"

// Meta is stored next to a golden xlsx as `<file>.xlsx.meta.json`.
type Meta struct {
	Host         string `json:"host"`
	ExcelVersion string `json:"excelVersion,omitempty"`
	ExcelBuild   string `json:"excelBuild,omitempty"`
	OS           string `json:"os"`
	Tool         string `json:"tool"`
	Input        string `json:"input"`
	GeneratedAt  string `json:"generatedAt"`
}

// PathFor returns the sidecar path for a golden xlsx.
func PathFor(xlsxPath string) string {
	return xlsxPath + ".meta.json"
}

// New fills timestamps and the tool name.
func New(host, osName, excelVersion, excelBuild, inputBase string) Meta {
	return Meta{
		Host:         host,
		ExcelVersion: excelVersion,
		ExcelBuild:   excelBuild,
		OS:           osName,
		Tool:         toolName,
		Input:        inputBase,
		GeneratedAt:  time.Now().UTC().Format(time.RFC3339),
	}
}

// Write encodes meta next to xlsxPath.
func Write(xlsxPath string, m Meta) error {
	if m.Tool == "" {
		m.Tool = toolName
	}
	if m.GeneratedAt == "" {
		m.GeneratedAt = time.Now().UTC().Format(time.RFC3339)
	}
	data, err := json.MarshalIndent(m, "", "  ")
	if err != nil {
		return err
	}
	data = append(data, '\n')
	if err := os.WriteFile(PathFor(xlsxPath), data, 0o644); err != nil {
		return fmt.Errorf("write golden meta: %w", err)
	}
	return nil
}

// Read loads the sidecar for xlsxPath.
func Read(xlsxPath string) (Meta, error) {
	data, err := os.ReadFile(PathFor(xlsxPath))
	if err != nil {
		return Meta{}, err
	}
	var m Meta
	if err := json.Unmarshal(data, &m); err != nil {
		return Meta{}, fmt.Errorf("golden meta: %w", err)
	}
	return m, nil
}
