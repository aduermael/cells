// Persistence - IndexedDB file persistence
// Handles persisting and restoring files using IndexedDB for data and
// localStorage for metadata.

import type { FileFormat } from "./types";

// =============================================================================
// Constants
// =============================================================================

const DB_NAME = "cells-viewer";
const DB_VERSION = 1;
const STORE_NAME = "files";
const FILE_KEY = "current";
const META_KEY = "cells-viewer-file-meta";

// =============================================================================
// Types
// =============================================================================

/** File metadata stored in localStorage */
export interface FileMeta {
  name: string;
  format: FileFormat;
  timestamp: number;
}

// =============================================================================
// FilePersistence Class
// =============================================================================

/**
 * FilePersistence manages file persistence using IndexedDB.
 *
 * Responsibilities:
 * - Storing file data in IndexedDB
 * - Loading file data from IndexedDB
 * - Clearing persisted files
 * - Managing file metadata in localStorage
 */
export class FilePersistence {
  private db: IDBDatabase | null = null;

  // =========================================================================
  // Database Operations
  // =========================================================================

  /**
   * Open the IndexedDB database
   */
  private async openDatabase(): Promise<IDBDatabase> {
    if (this.db) return this.db;

    return new Promise((resolve, reject) => {
      const request = indexedDB.open(DB_NAME, DB_VERSION);

      request.onerror = () => reject(request.error);
      request.onsuccess = () => {
        this.db = request.result;
        resolve(this.db);
      };

      request.onupgradeneeded = (e) => {
        const database = (e.target as IDBOpenDBRequest).result;
        if (!database.objectStoreNames.contains(STORE_NAME)) {
          database.createObjectStore(STORE_NAME);
        }
      };
    });
  }

  // =========================================================================
  // File Data Operations
  // =========================================================================

  /**
   * Save file data to IndexedDB
   * @param data - The file data as ArrayBuffer
   */
  async saveFileToIndexedDB(data: ArrayBuffer): Promise<void> {
    const database = await this.openDatabase();
    return new Promise((resolve, reject) => {
      const tx = database.transaction(STORE_NAME, "readwrite");
      const store = tx.objectStore(STORE_NAME);
      const request = store.put(data, FILE_KEY);
      request.onerror = () => reject(request.error);
      request.onsuccess = () => resolve();
    });
  }

  /**
   * Load file data from IndexedDB
   * @returns The file data as ArrayBuffer, or undefined if not found
   */
  async loadFileFromIndexedDB(): Promise<ArrayBuffer | undefined> {
    const database = await this.openDatabase();
    return new Promise((resolve, reject) => {
      const tx = database.transaction(STORE_NAME, "readonly");
      const store = tx.objectStore(STORE_NAME);
      const request = store.get(FILE_KEY);
      request.onerror = () => reject(request.error);
      request.onsuccess = () => resolve(request.result as ArrayBuffer | undefined);
    });
  }

  /**
   * Clear the persisted file from IndexedDB and localStorage
   */
  async clearPersistedFile(): Promise<void> {
    const database = await this.openDatabase();
    return new Promise((resolve, reject) => {
      const tx = database.transaction(STORE_NAME, "readwrite");
      const store = tx.objectStore(STORE_NAME);
      const request = store.delete(FILE_KEY);
      request.onerror = () => reject(request.error);
      request.onsuccess = () => {
        localStorage.removeItem(META_KEY);
        resolve();
      };
    });
  }

  // =========================================================================
  // File Metadata Operations
  // =========================================================================

  /**
   * Save file metadata to localStorage
   * @param name - The file name
   * @param format - The file format
   */
  saveFileMeta(name: string, format: FileFormat): void {
    localStorage.setItem(
      META_KEY,
      JSON.stringify({ name, format, timestamp: Date.now() })
    );
  }

  /**
   * Load file metadata from localStorage
   * @returns The file metadata, or null if not found
   */
  loadFileMeta(): FileMeta | null {
    const meta = localStorage.getItem(META_KEY);
    return meta ? (JSON.parse(meta) as FileMeta) : null;
  }
}

// =============================================================================
// Singleton Instance
// =============================================================================

/** Default persistence instance */
export const persistence = new FilePersistence();
