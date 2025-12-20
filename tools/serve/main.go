// Static file server for Cells WASM distribution
// Usage: go run tools/serve/main.go [options]
//
// This server is designed to serve the WASM distribution with correct
// MIME types and CORS headers for local development and testing.

package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"
)

func main() {
	port := flag.Int("port", 8081, "Port to listen on")
	dir := flag.String("dir", "dist", "Directory to serve")
	flag.Parse()

	// Resolve directory path
	absDir, err := filepath.Abs(*dir)
	if err != nil {
		log.Fatalf("Failed to resolve directory: %v", err)
	}

	// Check if directory exists
	if _, err := os.Stat(absDir); os.IsNotExist(err) {
		log.Fatalf("Directory does not exist: %s\nRun 'make wasm-dist' first.", absDir)
	}

	// Create file server with custom MIME types
	fs := http.FileServer(http.Dir(absDir))
	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// Set correct MIME type for .wasm files
		if filepath.Ext(r.URL.Path) == ".wasm" {
			w.Header().Set("Content-Type", "application/wasm")
		}

		// CORS headers for local development
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")

		// Handle preflight requests
		if r.Method == "OPTIONS" {
			w.WriteHeader(http.StatusOK)
			return
		}

		fs.ServeHTTP(w, r)
	})

	addr := fmt.Sprintf(":%d", *port)
	fmt.Printf("Serving %s on http://localhost%s/\n", absDir, addr)
	fmt.Println("Press Ctrl+C to stop")

	if err := http.ListenAndServe(addr, handler); err != nil {
		log.Fatalf("Server error: %v", err)
	}
}
