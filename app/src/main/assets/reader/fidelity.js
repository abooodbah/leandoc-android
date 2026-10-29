// Wording for the fidelity bar and the fallback pages. Pure functions, usable
// in the page (window.LeanDocText) and in Node for unit tests.
(function (root, factory) {
  var api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.LeanDocText = api;
  }
}(this, function () {
  "use strict";

  // Notices are named, not counted, and say what LeanDoc did with them.
  var NOTICES = {
    "tracked-changes": { phrase: "tracked changes (shown as accepted)", status: "Shown as accepted" },
    "comments": { phrase: "comments (hidden)", status: "Hidden" },
    "macros": { phrase: "macros (not run)", status: "Not run" },
    "remote-template": { phrase: "a remote template (not fetched)", status: "Not fetched" },
    "remote-image": { phrase: "remote images (not fetched)", status: "Not fetched" },
    "external-content": { phrase: "external content (not fetched)", status: "Not fetched" },
    "dde-field": { phrase: "DDE fields (not run)", status: "Not run" },
    "embedded-fonts": { phrase: "embedded fonts (system fonts used)", status: "System fonts used" }
  };

  // Used only when a report arrives without its own nouns.
  var NOUNS = {
    "text-box": ["text box", "text boxes"],
    "equation": ["equation", "equations"],
    "chart": ["chart", "charts"],
    "smart-art": ["SmartArt graphic", "SmartArt graphics"],
    "smartart": ["SmartArt graphic", "SmartArt graphics"],
    "shape": ["shape", "shapes"],
    "ole-object": ["embedded object", "embedded objects"],
    "activex": ["ActiveX control", "ActiveX controls"],
    "alt-chunk": ["imported content block", "imported content blocks"],
    "tracked-changes": ["tracked change", "tracked changes"],
    "comments": ["comment", "comments"],
    "macros": ["macro project", "macro projects"],
    "remote-template": ["remote template", "remote templates"],
    "remote-image": ["remote image", "remote images"],
    "external-content": ["external reference", "external references"],
    "dde-field": ["DDE field", "DDE fields"],
    "embedded-fonts": ["embedded font", "embedded fonts"]
  };

  function joinList(items) {
    if (items.length <= 1) {
      return items.join("");
    }
    if (items.length === 2) {
      return items[0] + " and " + items[1];
    }
    return items.slice(0, -1).join(", ") + ", and " + items[items.length - 1];
  }

  function capitalize(text) {
    return text ? text.charAt(0).toUpperCase() + text.slice(1) : text;
  }

  function nounFor(feature, count) {
    var fallback = NOUNS[feature.code] || [feature.code, feature.code];
    var singular = feature.singular || fallback[0];
    var plural = feature.plural || fallback[1];
    return count === 1 ? singular : plural;
  }

  function normalizeFeatures(report) {
    var list = report && Array.isArray(report.features) ? report.features : [];
    return list.filter(function (feature) {
      return feature && typeof feature.code === "string" &&
        Number(feature.count) > 0;
    }).map(function (feature) {
      return {
        code: feature.code,
        count: Math.floor(Number(feature.count)),
        notDisplayed: feature.notDisplayed === true,
        singular: typeof feature.singular === "string" ? feature.singular : "",
        plural: typeof feature.plural === "string" ? feature.plural : ""
      };
    });
  }

  // Returns null when the report lists no features: no bar is shown.
  function describeFidelity(report) {
    var features = normalizeFeatures(report);
    if (!features.length) {
      return null;
    }
    var hidden = features.filter(function (feature) {
      return feature.notDisplayed;
    });
    var notices = features.filter(function (feature) {
      return !feature.notDisplayed;
    });
    var sentences = [];
    if (hidden.length) {
      sentences.push("This document contains " + joinList(hidden.map(function (feature) {
        return feature.count + " " + nounFor(feature, feature.count);
      })) + " that LeanDoc does not display.");
    }
    if (notices.length) {
      var phrases = notices.map(function (feature) {
        var notice = NOTICES[feature.code];
        return notice ? notice.phrase : nounFor(feature, 2);
      });
      sentences.push((hidden.length ? "It also contains " : "This document contains ") +
        joinList(phrases) + ".");
    }
    var items = features.map(function (feature) {
      var notice = NOTICES[feature.code];
      return {
        code: feature.code,
        count: feature.count,
        label: capitalize(nounFor(feature, feature.count)),
        status: feature.notDisplayed
          ? "Not displayed"
          : notice ? notice.status : "Noted"
      };
    });
    var summary = hidden.length
      ? "Some content in this document is not displayed."
      : "This document has " + (notices.length === 1 ? "a notice." : "notices.");
    return {
      sentences: sentences,
      text: sentences.join(" "),
      summary: summary,
      hasHidden: hidden.length > 0,
      items: items
    };
  }

  var KIND_LABELS = {
    "legacy-doc": "Word 97-2003 document",
    "encrypted-ooxml": "Password-protected document",
    "rtf": "Rich Text Format document",
    "odt": "OpenDocument text",
    "not-a-document": "Not a Word document"
  };

  var KIND_SENTENCES = {
    "legacy-doc": "This is a Word 97-2003 document (.doc). LeanDoc 0.1 reads .docx files only.",
    "encrypted-ooxml": "This document is password protected. LeanDoc does not open protected documents.",
    "rtf": "This is a Rich Text Format document (.rtf). LeanDoc 0.1 reads .docx files only.",
    "odt": "This is an OpenDocument text file (.odt). LeanDoc 0.1 reads .docx files only.",
    "not-a-document": "This file is not a Word document. LeanDoc reads .docx files only."
  };

  var ERROR_SENTENCES = {
    "too-large": "This file is larger than LeanDoc's 256 MB limit.",
    "not-a-zip": "This file is not a valid Word document.",
    "corrupt-zip": "This document is damaged and cannot be read.",
    "too-many-entries": "This document has more parts than LeanDoc reads.",
    "part-too-large": "A part of this document is larger than LeanDoc's limit.",
    "compression-ratio": "This document expands far beyond its file size, so LeanDoc stopped reading it.",
    "unsafe-entry-name": "This document contains a part with an unsafe name, so LeanDoc stopped reading it.",
    "duplicate-entry": "This document contains duplicate parts, so LeanDoc stopped reading it.",
    "encrypted-entry": "This document contains encrypted parts. LeanDoc does not open protected documents.",
    "missing-main-part": "This file has no Word document body.",
    "xml-doctype": "This document contains a document type declaration, which LeanDoc refuses to read.",
    "xml-malformed": "This document contains malformed XML and cannot be read.",
    "xml-too-deep": "This document is nested more deeply than LeanDoc reads.",
    "read-failed": "LeanDoc could not read this file.",
    "renderer-stopped": "LeanDoc's renderer stopped several times while showing this document. Press F5 to try again."
  };

  // A fallback page: a short title and one sentence on what the file is and
  // why it is not shown. Core errors prefer the core's own sentence.
  function describeFallback(report) {
    var kind = report && typeof report.kind === "string" ? report.kind : "";
    var errorCode = report && typeof report.errorCode === "string" ? report.errorCode : "";
    var errorMessage = report && typeof report.errorMessage === "string"
      ? report.errorMessage.trim()
      : "";
    if (KIND_SENTENCES[kind] && !errorCode) {
      return { title: KIND_LABELS[kind], message: KIND_SENTENCES[kind], code: kind };
    }
    if (errorCode || (report && report.ok === false)) {
      return {
        title: "This document could not be opened",
        message: errorMessage || ERROR_SENTENCES[errorCode] ||
          "LeanDoc could not read this document.",
        code: errorCode || "error"
      };
    }
    if (KIND_SENTENCES[kind]) {
      return { title: KIND_LABELS[kind], message: KIND_SENTENCES[kind], code: kind };
    }
    return {
      title: "This document could not be shown",
      message: errorMessage || "LeanDoc could not show this document.",
      code: kind || "unknown"
    };
  }

  function editorActionLabel(label) {
    var text = typeof label === "string" ? label.trim() : "";
    if (!text) {
      return "";
    }
    return /^open\b/i.test(text) ? text : "Open in " + text;
  }

  return Object.freeze({
    joinList: joinList,
    describeFidelity: describeFidelity,
    describeFallback: describeFallback,
    editorActionLabel: editorActionLabel,
    NOTICES: NOTICES
  });
}));
