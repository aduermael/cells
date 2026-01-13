/**
 * Font Loader - Dynamic web font loading from Google Fonts
 *
 * Checks if a font is available locally and loads it from Google Fonts if not.
 * Provides fallback font mapping for common spreadsheet fonts.
 */

// Mapping of common fonts to their Google Fonts equivalent or closest fallback
// Some fonts (like Calibri) are proprietary and need a substitute
const GOOGLE_FONTS_MAP: Record<string, string | null> = {
  // Microsoft Office fonts with Google Fonts substitutes
  Calibri: "Carlito", // Carlito is metrically compatible with Calibri
  Cambria: "Caladea", // Caladea is metrically compatible with Cambria
  "Calibri Light": "Carlito",

  // Standard fonts available on Google Fonts
  Arial: null, // System font, widely available
  "Times New Roman": null, // System font, widely available
  Georgia: "Georgia", // Available on Google Fonts
  Verdana: null, // System font, widely available
  "Trebuchet MS": null, // System font, often available
  "Courier New": null, // System font, widely available
  Helvetica: null, // System font (macOS) or falls back to Arial

  // Google Fonts that can be loaded directly
  Roboto: "Roboto",
  "Open Sans": "Open Sans",
  Lato: "Lato",
  Montserrat: "Montserrat",
  "Source Sans Pro": "Source Sans 3",
  "PT Sans": "PT Sans",
  "Noto Sans": "Noto Sans",
  "Fira Sans": "Fira Sans",

  // Serif fonts on Google Fonts
  "Roboto Slab": "Roboto Slab",
  Merriweather: "Merriweather",
  "Playfair Display": "Playfair Display",
  Lora: "Lora",
  "Source Serif Pro": "Source Serif 4",
  "PT Serif": "PT Serif",
  "Noto Serif": "Noto Serif",

  // Monospace fonts on Google Fonts
  "Roboto Mono": "Roboto Mono",
  "Source Code Pro": "Source Code Pro",
  "Fira Code": "Fira Code",
  "JetBrains Mono": "JetBrains Mono",
  Inconsolata: "Inconsolata",
};

// Fallback font stack when a font can't be loaded
const FALLBACK_FONTS =
  '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif';

// Cache of fonts we've already checked/loaded
const fontCache = new Map<
  string,
  { available: boolean; googleFont: string | null; loading: Promise<void> | null }
>();

// Set of fonts currently being loaded (to prevent duplicate requests)
const loadingFonts = new Set<string>();

// Callbacks to invoke when fonts are loaded (for re-rendering)
const fontLoadCallbacks: (() => void)[] = [];

/**
 * Register a callback to be called when a font finishes loading.
 * Useful for triggering re-renders after fonts become available.
 */
export function onFontLoaded(callback: () => void): () => void {
  fontLoadCallbacks.push(callback);
  // Return unsubscribe function
  return () => {
    const index = fontLoadCallbacks.indexOf(callback);
    if (index !== -1) {
      fontLoadCallbacks.splice(index, 1);
    }
  };
}

/**
 * Notify all registered callbacks that a font has loaded.
 */
function notifyFontLoaded(): void {
  for (const callback of fontLoadCallbacks) {
    try {
      callback();
    } catch (e) {
      console.error("Error in font load callback:", e);
    }
  }
}

/**
 * Check if a font is available in the browser.
 * Uses the CSS Font Loading API when available.
 */
function isFontAvailable(fontFamily: string): boolean {
  // Use the CSS Font Loading API if available
  if (document.fonts && typeof document.fonts.check === "function") {
    // Check at a reasonable size - some fonts may not load at very small sizes
    return document.fonts.check(`16px "${fontFamily}"`);
  }

  // Fallback: canvas-based detection
  // This works by comparing the width of text rendered in the target font
  // vs a known fallback font
  const canvas = document.createElement("canvas");
  const ctx = canvas.getContext("2d");
  if (!ctx) return false;

  const testString = "mmmmmmmmmmlli";
  const fallbackFont = "monospace";

  ctx.font = `72px ${fallbackFont}`;
  const fallbackWidth = ctx.measureText(testString).width;

  ctx.font = `72px "${fontFamily}", ${fallbackFont}`;
  const testWidth = ctx.measureText(testString).width;

  // If widths differ, the font is available
  return testWidth !== fallbackWidth;
}

/**
 * Load a font from Google Fonts.
 * Returns a promise that resolves when the font is loaded.
 */
async function loadGoogleFont(fontName: string): Promise<void> {
  if (loadingFonts.has(fontName)) {
    // Already loading this font
    return;
  }

  loadingFonts.add(fontName);

  try {
    // Create the Google Fonts URL
    // Format: https://fonts.googleapis.com/css2?family=Font+Name:wght@400;700&display=swap
    const encodedName = encodeURIComponent(fontName);
    const url = `https://fonts.googleapis.com/css2?family=${encodedName}:ital,wght@0,400;0,700;1,400;1,700&display=swap`;

    // Check if we already have a link for this font
    const existingLink = document.querySelector(`link[href*="${encodedName}"]`);
    if (existingLink) {
      loadingFonts.delete(fontName);
      return;
    }

    // Create and add the link element
    const link = document.createElement("link");
    link.rel = "stylesheet";
    link.href = url;

    // Wait for the stylesheet to load
    await new Promise<void>((resolve, reject) => {
      link.onload = () => resolve();
      link.onerror = () => reject(new Error(`Failed to load font: ${fontName}`));
      document.head.appendChild(link);
    });

    // Wait for the font to actually be ready
    if (document.fonts && typeof document.fonts.load === "function") {
      await document.fonts.load(`16px "${fontName}"`);
    }

    // Notify that a font has loaded
    notifyFontLoaded();
  } catch (e) {
    console.warn(`Failed to load font "${fontName}" from Google Fonts:`, e);
  } finally {
    loadingFonts.delete(fontName);
  }
}

/**
 * Ensure a font is available for rendering.
 *
 * If the font is not available locally, attempts to load it from Google Fonts.
 * Returns the font family string to use (either the original or a substitute).
 *
 * @param fontFamily - The desired font family name
 * @returns The font family to use (may be original, substitute, or fallback)
 */
export function ensureFont(fontFamily: string): string {
  // Empty or default - use system fallback
  if (!fontFamily || fontFamily === "default") {
    return FALLBACK_FONTS;
  }

  // Check cache first
  const cached = fontCache.get(fontFamily);
  if (cached) {
    if (cached.available) {
      return fontFamily;
    }
    if (cached.googleFont) {
      // Return Google Font substitute if it's loaded
      if (isFontAvailable(cached.googleFont)) {
        return cached.googleFont;
      }
      // Still loading, return fallback for now
      return FALLBACK_FONTS;
    }
    // No Google Font substitute available
    return FALLBACK_FONTS;
  }

  // Check if font is already available locally
  if (isFontAvailable(fontFamily)) {
    fontCache.set(fontFamily, { available: true, googleFont: null, loading: null });
    return fontFamily;
  }

  // Font not available - look for Google Fonts mapping
  const googleFont = GOOGLE_FONTS_MAP[fontFamily];

  if (googleFont === undefined) {
    // Not in our map - try to load it directly from Google Fonts
    // Many fonts are available with their exact name
    fontCache.set(fontFamily, { available: false, googleFont: fontFamily, loading: null });
    loadGoogleFont(fontFamily);
    return FALLBACK_FONTS;
  }

  if (googleFont === null) {
    // System font that should be available - use fallback
    fontCache.set(fontFamily, { available: false, googleFont: null, loading: null });
    return FALLBACK_FONTS;
  }

  // We have a Google Font substitute
  // Check if the substitute is already available
  if (isFontAvailable(googleFont)) {
    fontCache.set(fontFamily, { available: false, googleFont, loading: null });
    return googleFont;
  }

  // Need to load the Google Font
  fontCache.set(fontFamily, { available: false, googleFont, loading: null });
  loadGoogleFont(googleFont);

  return FALLBACK_FONTS;
}

/**
 * Preload fonts that are commonly used in spreadsheets.
 * Call this during app initialization for better performance.
 */
export function preloadCommonFonts(): void {
  // Preload fonts commonly used in Excel files
  const commonFonts = ["Carlito", "Caladea"]; // Calibri and Cambria substitutes

  for (const font of commonFonts) {
    if (!isFontAvailable(font)) {
      loadGoogleFont(font);
    }
  }
}

/**
 * Get the fallback font stack.
 */
export function getFallbackFonts(): string {
  return FALLBACK_FONTS;
}

/**
 * Check if a specific font is currently available (loaded or system).
 */
export function isFontLoaded(fontFamily: string): boolean {
  return isFontAvailable(fontFamily);
}

/**
 * Clear the font cache. Useful for testing.
 */
export function clearFontCache(): void {
  fontCache.clear();
}
