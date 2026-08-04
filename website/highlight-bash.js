/**
 * Lightweight bash/shell syntax highlighter for the landing page.
 * No CDN — apply to <pre class="code-block" data-lang="bash"><code>…</code></pre>
 */
(function (root) {
  "use strict";

  function escapeHtml(s) {
    return s
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  function highlightBash(src) {
    var out = "";
    var i = 0;
    var n = src.length;

    function takeWhile(pred) {
      var start = i;
      while (i < n && pred(src.charAt(i))) i++;
      return src.slice(start, i);
    }

    while (i < n) {
      var ch = src.charAt(i);

      // Line comments
      if (ch === "#") {
        var cstart = i;
        while (i < n && src.charAt(i) !== "\n") i++;
        out += '<span class="tok-comment">' + escapeHtml(src.slice(cstart, i)) + "</span>";
        continue;
      }

      // Single-quoted strings
      if (ch === "'") {
        var s1 = i;
        i++;
        while (i < n && src.charAt(i) !== "'") i++;
        if (i < n) i++;
        out += '<span class="tok-string">' + escapeHtml(src.slice(s1, i)) + "</span>";
        continue;
      }

      // Double-quoted strings
      if (ch === '"') {
        var s2 = i;
        i++;
        while (i < n) {
          if (src.charAt(i) === "\\" && i + 1 < n) {
            i += 2;
            continue;
          }
          if (src.charAt(i) === '"') {
            i++;
            break;
          }
          i++;
        }
        out += '<span class="tok-string">' + escapeHtml(src.slice(s2, i)) + "</span>";
        continue;
      }

      // Flags: -i, --script, etc.
      if (ch === "-" && (i === 0 || /[\s|]/.test(src.charAt(i - 1)))) {
        var flag = takeWhile(function (c) {
          return /[A-Za-z0-9_=-]/.test(c);
        });
        out += '<span class="tok-flag">' + escapeHtml(flag) + "</span>";
        continue;
      }

      // Words: commands / paths / bare tokens
      if (/[A-Za-z0-9_./:@]/.test(ch)) {
        var word = takeWhile(function (c) {
          return /[A-Za-z0-9_./:@+-]/.test(c);
        });
        var cls = null;
        if (/^(cells|curl|brew|bazel|sh|bash)$/.test(word)) cls = "tok-cmd";
        else if (/^(install|run|fsSL)$/.test(word)) cls = "tok-cmd";
        else if (/^(https?:\/\/|.*\.(sh|luau|csv|xlsx|zcd))$/.test(word)) cls = "tok-path";
        else if (word.indexOf("://") >= 0) cls = "tok-path";
        if (cls) {
          out += '<span class="' + cls + '">' + escapeHtml(word) + "</span>";
        } else {
          out += escapeHtml(word);
        }
        continue;
      }

      if (ch === "|") {
        out += '<span class="tok-op">' + escapeHtml(ch) + "</span>";
        i++;
        continue;
      }

      out += escapeHtml(ch);
      i++;
    }
    return out;
  }

  function highlightAll(doc) {
    doc = doc || (typeof document !== "undefined" ? document : null);
    if (!doc || !doc.querySelectorAll) return;
    var blocks = doc.querySelectorAll("pre.code-block[data-lang] > code");
    for (var b = 0; b < blocks.length; b++) {
      var code = blocks[b];
      var lang = code.parentNode.getAttribute("data-lang");
      if (lang === "bash" || lang === "shell" || lang === "sh") {
        code.innerHTML = highlightBash(code.textContent);
        code.parentNode.classList.add("hl");
      }
    }
  }

  var api = {
    escapeHtml: escapeHtml,
    highlightBash: highlightBash,
    highlightAll: highlightAll,
  };

  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
  root.CellsHighlight = api;

  // Auto-run in browser when DOM is ready enough (script is at end of body).
  if (typeof document !== "undefined" && document.querySelectorAll) {
    highlightAll(document);
  }
})(typeof globalThis !== "undefined" ? globalThis : this);
