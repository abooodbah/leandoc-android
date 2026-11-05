// Connects the desktop reader page (reader.js, unchanged) to the Android app.
//
// reader.js posts JSON text to its host through
// window.webkit.messageHandlers.leandoc, as on the desktop WebKit hosts, and
// receives through LeanDocHost.receive. Here both directions travel over a
// MessagePort that the app hands to the page once it has loaded. Messages
// posted before that wait in a queue.
(function () {
  "use strict";

  var port = null;
  var queue = [];

  // reader.js uses Element.replaceChildren, which WebView has from version 86.
  // The page needs WebView 80 for docx-preview; versions 80 to 85 get this
  // equivalent.
  if (!Element.prototype.replaceChildren) {
    Element.prototype.replaceChildren = function () {
      while (this.firstChild) {
        this.removeChild(this.firstChild);
      }
      this.append.apply(this, arguments);
    };
  }

  function send(message) {
    var text = String(message);
    if (port) {
      port.postMessage(text);
    } else {
      queue.push(text);
    }
  }

  window.webkit = Object.freeze({
    messageHandlers: Object.freeze({
      leandoc: Object.freeze({ postMessage: send })
    })
  });

  // Messages for this file rather than the reader. The app sends this one
  // when the back gesture should close an open sheet.
  var CLOSE_SHEET = '{"type":"closeSheet"}';

  function receive(event) {
    if (event.data === CLOSE_SHEET) {
      closeSheet(true);
    } else if (window.LeanDocHost && typeof event.data === "string") {
      window.LeanDocHost.receive(event.data);
    }
  }

  // The app posts exactly one port, right after the page loads. The page has
  // no frames and runs no document scripts, so nothing else can post here.
  window.addEventListener("message", function (event) {
    if (port || event.data !== "leandoc-port" || !event.ports ||
        event.ports.length !== 1) {
      return;
    }
    port = event.ports[0];
    port.onmessage = receive;
    queue.splice(0).forEach(function (message) {
      port.postMessage(message);
    });
  });

  // How long reader actions take to reach the screen, for scripts/measure.sh.
  // Each time runs from the key, tap or typed letter to the second animation
  // frame after it: by then the frame with the action's style and layout work
  // has been drawn. A resize, such as a rotation, is timed to the frame after
  // the reader applies the new zoom (it waits for the size to settle), or to
  // the frame after the resize when the zoom stays the same. View switches
  // are timed by the reader itself, in its "rendered" message.
  function afterNextFrame(callback) {
    window.requestAnimationFrame(function () {
      window.requestAnimationFrame(callback);
    });
  }

  function timeAction(action, started) {
    afterNextFrame(function () {
      send(JSON.stringify({
        type: "timing",
        action: action,
        ms: Math.round(performance.now() - started)
      }));
    });
  }

  function keyAction(event) {
    if (!event.ctrlKey || event.altKey) {
      return "";
    }
    var key = typeof event.key === "string" ? event.key.toLowerCase() : "";
    if (!event.shiftKey && (key === "+" || key === "=" || key === "-" || key === "_" || key === "0")) {
      return "zoom";
    }
    return event.shiftKey && key === "t" ? "theme" : "";
  }

  var CLICK_ACTIONS = { zoomIn: "zoom", zoomOut: "zoom", zoomValue: "zoom", themeButton: "theme" };

  if (window.performance && performance.now && window.requestAnimationFrame) {
    window.addEventListener("keydown", function (event) {
      var action = keyAction(event);
      if (action) {
        timeAction(action, performance.now());
      }
    }, true);
    window.addEventListener("click", function (event) {
      var button = event.target && event.target.closest ? event.target.closest("button") : null;
      var action = button ? CLICK_ACTIONS[button.id] : "";
      if (action) {
        timeAction(action, performance.now());
      }
    }, true);
    window.addEventListener("input", function (event) {
      if (event.target && event.target.id === "findInput") {
        timeAction("find", performance.now());
      }
    }, true);

    var resizeStarted = 0;
    var resizeDrawn = 0;
    var resizeTimer = 0;
    var finishResize = function (ms) {
      resizeStarted = 0;
      window.clearTimeout(resizeTimer);
      send(JSON.stringify({ type: "timing", action: "resize", ms: Math.round(ms) }));
    };
    window.addEventListener("resize", function () {
      if (resizeStarted) {
        return;
      }
      var started = performance.now();
      resizeStarted = started;
      resizeDrawn = 0;
      afterNextFrame(function () {
        if (resizeStarted === started) {
          resizeDrawn = performance.now();
        }
      });
      // The reader applies a new zoom 150 ms after the last resize event.
      // When the zoom stays the same, the time is the first frame's.
      resizeTimer = window.setTimeout(function () {
        if (resizeStarted === started) {
          finishResize((resizeDrawn || performance.now()) - started);
        }
      }, 1000);
    });
    if (window.MutationObserver) {
      new MutationObserver(function () {
        var started = resizeStarted;
        if (started) {
          // The zoom's layout may outlast the timer; this frame decides.
          window.clearTimeout(resizeTimer);
          afterNextFrame(function () {
            if (resizeStarted === started) {
              finishResize(performance.now() - started);
            }
          });
        }
      }).observe(document.documentElement, { attributes: true, attributeFilter: ["data-zoom"] });
    }
  }

  // On a phone the desktop toolbar wraps to three rows under the file name.
  // Below 760 px the theme, settings, other-app and Open buttons move up
  // beside the file name (android.css lays out the two rows). The buttons
  // keep their ids and handlers; they move back into the toolbar when the
  // page is wider again, as in landscape on a tablet.
  //
  // The desktop page hides its outline below 760 px. On a phone the outline
  // button opens the same outline pane in a sheet from the bottom edge, so
  // the reader's highlight of the current section, and its scrolling of the
  // list to keep that entry in view, work there as in the pane. Android's
  // back gesture closes an open sheet first.
  var SVG = "http://www.w3.org/2000/svg";

  function icon(paths) {
    var svg = document.createElementNS(SVG, "svg");
    svg.setAttribute("viewBox", "0 0 16 16");
    svg.setAttribute("aria-hidden", "true");
    paths.forEach(function (d) {
      var path = document.createElementNS(SVG, "path");
      path.setAttribute("d", d);
      svg.appendChild(path);
    });
    return svg;
  }

  var sheets = {
    open: null,
    returnFocus: null
  };

  function tellApp(open) {
    send(JSON.stringify({ type: "sheet", open: open }));
  }

  function closeSheet(restoreFocus) {
    var sheet = sheets.open;
    if (!sheet) {
      return;
    }
    sheets.open = null;
    sheet.element.hidden = true;
    sheet.scrim.hidden = true;
    sheet.button.setAttribute("aria-expanded", "false");
    delete document.documentElement.dataset.sheet;
    tellApp(false);
    var target = sheets.returnFocus;
    sheets.returnFocus = null;
    if (restoreFocus && target && target.isConnected) {
      target.focus({ preventScroll: true });
    }
  }

  function openSheet(sheet) {
    if (sheets.open === sheet) {
      return;
    }
    closeSheet(false);
    sheets.open = sheet;
    sheets.returnFocus = sheet.button;
    sheet.element.hidden = false;
    sheet.scrim.hidden = false;
    sheet.button.setAttribute("aria-expanded", "true");
    document.documentElement.dataset.sheet = sheet.name;
    tellApp(true);
    sheet.opened();
  }

  function makeSheet(name, title, button, opened) {
    var scrim = document.createElement("div");
    scrim.className = "android-scrim";
    scrim.hidden = true;
    var element = document.createElement("section");
    element.className = "android-sheet";
    element.id = name + "Sheet";
    element.hidden = true;
    element.setAttribute("role", "dialog");
    element.setAttribute("aria-modal", "true");
    var head = document.createElement("div");
    head.className = "android-sheet-head";
    var heading = document.createElement("h2");
    heading.id = name + "SheetTitle";
    heading.textContent = title;
    element.setAttribute("aria-labelledby", heading.id);
    var close = document.createElement("button");
    close.type = "button";
    close.className = "android-sheet-close";
    close.setAttribute("aria-label", "Close");
    close.appendChild(icon(["M4 4l8 8M12 4l-8 8"]));
    head.appendChild(heading);
    head.appendChild(close);
    element.appendChild(head);
    document.body.appendChild(scrim);
    document.body.appendChild(element);
    var sheet = {
      name: name,
      element: element,
      scrim: scrim,
      heading: heading,
      button: button,
      opened: opened
    };
    scrim.addEventListener("click", function () {
      closeSheet(true);
    });
    close.addEventListener("click", function () {
      closeSheet(true);
    });
    button.setAttribute("aria-haspopup", "dialog");
    button.setAttribute("aria-expanded", "false");
    button.setAttribute("aria-controls", element.id);
    button.addEventListener("click", function () {
      if (sheets.open === sheet) {
        closeSheet(true);
      } else {
        openSheet(sheet);
      }
    });
    return sheet;
  }

  // Scrolls the outline list so the current entry sits a third of the way
  // down, as the reader does when the entry leaves the list's view.
  function showCurrentOutlineEntry(list) {
    var link = list.querySelector('a[aria-current="location"]');
    if (!link) {
      return null;
    }
    var top = link.offsetTop - list.offsetTop;
    if (top < list.scrollTop || top + link.offsetHeight > list.scrollTop + list.clientHeight) {
      list.scrollTop = Math.max(0, top - list.clientHeight / 3);
    }
    return link;
  }

  function setUpOutlineSheet(toolbarSlot, phone) {
    var outline = document.getElementById("outline");
    var list = document.getElementById("outlineList");
    var label = document.getElementById("outlineLabel");
    if (!outline || !list) {
      return;
    }
    var home = outline.parentNode;
    var next = outline.nextSibling;
    var title = label ? label.textContent.trim() : "Outline";
    var button = document.createElement("button");
    button.id = "outlineButton";
    button.type = "button";
    button.className = "button button-ghost android-icon-button doc-control";
    button.setAttribute("aria-label", title);
    button.title = title;
    button.hidden = outline.hidden;
    button.appendChild(icon(["M2.5 4h1M6 4h7.5M2.5 8h1M6 8h7.5M2.5 12h1M6 12h7.5"]));
    toolbarSlot(button);
    var sheet = makeSheet("outline", title, button, function () {
      var link = showCurrentOutlineEntry(list);
      var first = link || list.querySelector("a");
      (first || sheet.element.querySelector(".android-sheet-close")).focus({ preventScroll: true });
    });
    // The reader hides the outline when the document has no headings.
    new MutationObserver(function () {
      button.hidden = outline.hidden;
      if (outline.hidden && sheets.open === sheet) {
        closeSheet(false);
      }
    }).observe(outline, { attributes: true, attributeFilter: ["hidden"] });
    // A tapped entry: the reader has scrolled to it (its handler runs first,
    // as reader.js registered it first); the sheet gives way to the page.
    list.addEventListener("click", function (event) {
      if (sheets.open === sheet && event.target && event.target.closest && event.target.closest("a")) {
        closeSheet(false);
      }
    });
    function place() {
      if (phone.matches) {
        sheet.element.appendChild(outline);
      } else {
        if (sheets.open === sheet) {
          closeSheet(false);
        }
        home.insertBefore(outline, next && next.parentNode === home ? next : null);
        // Back in the pane, as after turning a phone to landscape: the list
        // starts at the top, so the current entry is brought into view.
        afterNextFrame(function () {
          showCurrentOutlineEntry(list);
        });
      }
    }
    place();
    return place;
  }

  // The settings sheet: theme, view and zoom, as in LeanMark's settings
  // panel. Each choice works through the reader's own controls and host
  // messages, so the reader, the app and the sheet agree: a theme goes to
  // the reader as the app sends it and to the app as the theme button does,
  // the view and zoom buttons are the toolbar's.
  var THEMES = ["system", "light", "dark", "dim", "dusk"];

  function choice(group, value, text, swatch) {
    var label = document.createElement("label");
    label.className = "settings-choice";
    var input = document.createElement("input");
    input.type = "radio";
    input.name = "settings-" + group;
    input.value = value;
    label.appendChild(input);
    if (swatch) {
      var dot = document.createElement("span");
      dot.className = "theme-swatch";
      dot.dataset.swatch = value;
      dot.setAttribute("aria-hidden", "true");
      label.appendChild(dot);
    }
    label.appendChild(document.createTextNode(text));
    return label;
  }

  function fieldset(legend, children) {
    var group = document.createElement("fieldset");
    group.className = "settings-group";
    var title = document.createElement("legend");
    title.textContent = legend;
    group.appendChild(title);
    children.forEach(function (child) {
      group.appendChild(child);
    });
    return group;
  }

  function setUpSettingsSheet(phone) {
    var root = document.documentElement;
    var zoomIn = document.getElementById("zoomIn");
    var zoomOut = document.getElementById("zoomOut");
    var zoomValue = document.getElementById("zoomValue");
    var viewPage = document.getElementById("viewPage");
    var viewReading = document.getElementById("viewReading");
    var chrome = document.getElementById("chrome");
    if (!zoomIn || !zoomOut || !zoomValue || !viewPage || !viewReading) {
      return null;
    }
    var button = document.createElement("button");
    button.id = "settingsButton";
    button.type = "button";
    button.className = "button button-ghost android-icon-button";
    button.setAttribute("aria-label", "Settings");
    button.title = "Settings";
    // LeanMark's settings icon: three sliders.
    var sliders = icon(["M2 4h6M11 4h3M2 8h2M7 8h7M2 12h7M12 12h2"]);
    [[9.5, 4], [5.5, 8], [10.5, 12]].forEach(function (centre) {
      var knob = document.createElementNS(SVG, "circle");
      knob.setAttribute("cx", String(centre[0]));
      knob.setAttribute("cy", String(centre[1]));
      knob.setAttribute("r", "1.5");
      sliders.appendChild(knob);
    });
    button.appendChild(sliders);

    var themes = fieldset("Theme", THEMES.map(function (theme) {
      return choice("theme", theme, theme.charAt(0).toUpperCase() + theme.slice(1), true);
    }));
    var views = fieldset("View", [choice("view", "page", "Page"), choice("view", "reading", "Reading")]);
    var zoom = document.createElement("div");
    zoom.className = "settings-group settings-zoom";
    zoom.setAttribute("role", "group");
    zoom.setAttribute("aria-labelledby", "settingsZoomLabel");
    var zoomLabel = document.createElement("span");
    zoomLabel.id = "settingsZoomLabel";
    zoomLabel.className = "settings-label";
    zoomLabel.textContent = "Zoom";
    var out = document.createElement("button");
    out.id = "settingsZoomOut";
    out.type = "button";
    out.className = "button button-secondary";
    out.setAttribute("aria-label", "Zoom out");
    out.appendChild(icon(["M3.5 8h9"]));
    var value = document.createElement("output");
    value.id = "settingsZoomValue";
    value.className = "settings-zoom-value";
    value.setAttribute("aria-live", "polite");
    var into = document.createElement("button");
    into.id = "settingsZoomIn";
    into.type = "button";
    into.className = "button button-secondary";
    into.setAttribute("aria-label", "Zoom in");
    into.appendChild(icon(["M3.5 8h9M8 3.5v9"]));
    var fit = document.createElement("button");
    fit.id = "settingsZoomFit";
    fit.type = "button";
    fit.className = "button button-secondary";
    [zoomLabel, out, value, into, fit].forEach(function (child) {
      zoom.appendChild(child);
    });
    var body = document.createElement("div");
    body.className = "android-sheet-body";
    [themes, views, zoom].forEach(function (child) {
      body.appendChild(child);
    });

    function update() {
      var theme = root.getAttribute("data-theme") || "system";
      var view = root.dataset.view === "reading" ? "reading" : "page";
      var hasDocument = root.hasAttribute("data-has-document");
      body.querySelectorAll('input[name="settings-theme"]').forEach(function (input) {
        input.checked = input.value === theme;
      });
      body.querySelectorAll('input[name="settings-view"]').forEach(function (input) {
        input.checked = input.value === view;
        input.disabled = !hasDocument;
      });
      value.textContent = hasDocument && root.dataset.zoom ? root.dataset.zoom + "%" : "";
      out.disabled = !hasDocument || zoomOut.disabled;
      into.disabled = !hasDocument || zoomIn.disabled;
      fit.disabled = !hasDocument || view === "reading";
      var fitted = root.dataset.zoomMode === "fit";
      fit.textContent = fitted ? "100%" : "Fit width";
      fit.setAttribute("aria-label", fitted ? "Zoom to 100%" : "Fit the page to the width");
    }

    var sheet = makeSheet("settings", "Settings", button, function () {
      // Beside the document below the toolbar on a wide page; from the
      // bottom edge on a phone.
      sheet.element.style.top = phone.matches || !chrome ? "" : chrome.getBoundingClientRect().bottom + "px";
      update();
      var checked = body.querySelector('input[name="settings-theme"]:checked');
      (checked || sheet.element.querySelector(".android-sheet-close")).focus({ preventScroll: true });
    });
    sheet.element.appendChild(body);

    body.addEventListener("change", function (event) {
      var input = event.target;
      if (input.name === "settings-theme" && THEMES.indexOf(input.value) >= 0) {
        // The reader shows it now; the app keeps it and colours its bars.
        if (window.LeanDocHost) {
          window.LeanDocHost.receive(JSON.stringify({ type: "theme", theme: input.value }));
        }
        send(JSON.stringify({ type: "setTheme", theme: input.value }));
      } else if (input.name === "settings-view") {
        (input.value === "reading" ? viewReading : viewPage).click();
      }
    });
    out.addEventListener("click", function () {
      zoomOut.click();
    });
    into.addEventListener("click", function () {
      zoomIn.click();
    });
    fit.addEventListener("click", function () {
      zoomValue.click();
    });
    new MutationObserver(function () {
      if (sheets.open === sheet) {
        update();
      }
    }).observe(root, {
      attributes: true,
      attributeFilter: ["data-theme", "data-view", "data-zoom", "data-zoom-mode", "data-has-document"]
    });
    var closeOnResize = function () {
      if (sheets.open === sheet) {
        closeSheet(false);
      }
    };
    if (phone.addEventListener) {
      phone.addEventListener("change", closeOnResize);
    } else if (phone.addListener) {
      phone.addListener(closeOnResize);
    }
    return button;
  }

  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape" && sheets.open) {
      event.preventDefault();
      event.stopPropagation();
      closeSheet(true);
    }
  }, true);

  // The reader picks the current section in the first frame after each
  // scroll event. After a long jump, the pages that come into view are still
  // waiting to be laid out in that frame (they use content-visibility), so
  // their headings are not where they will be, and the highlight stayed on a
  // section several pages away until the next scroll. Once scrolling stops,
  // the reader's scroll handlers run once more, on the laid-out pages.
  function recheckAfterScrolling() {
    var scroller = document.getElementById("docScroller");
    if (!scroller) {
      return;
    }
    var timer = 0;
    scroller.addEventListener("scroll", function (event) {
      if (!event.isTrusted) {
        return;
      }
      window.clearTimeout(timer);
      timer = window.setTimeout(function () {
        afterNextFrame(function () {
          scroller.dispatchEvent(new Event("scroll"));
        });
      }, 120);
    }, { passive: true });
  }

  document.addEventListener("DOMContentLoaded", function () {
    recheckAfterScrolling();
    var toolbar = document.querySelector(".toolbar");
    var identity = document.querySelector(".identity");
    var moved = ["themeButton", "editorButton", "openButton"].map(function (id) {
      return document.getElementById(id);
    }).filter(Boolean);
    if (!toolbar || !identity || !window.matchMedia) {
      return;
    }
    var actions = document.createElement("div");
    actions.className = "phone-actions";
    actions.setAttribute("role", "group");
    actions.setAttribute("aria-label", "Document actions");
    var phone = window.matchMedia("(max-width: 760px)");
    var search = toolbar.querySelector(".search-field");
    var placeOutline = setUpOutlineSheet(function (button) {
      toolbar.insertBefore(button, search ? search.nextSibling : toolbar.firstChild);
    }, phone);
    var settingsButton = setUpSettingsSheet(phone);
    var themeButton = document.getElementById("themeButton");
    if (settingsButton) {
      // Beside the theme button, in the toolbar or beside the file name.
      moved.splice(moved.indexOf(themeButton) + 1, 0, settingsButton);
      toolbar.insertBefore(settingsButton, themeButton ? themeButton.nextSibling : null);
    }
    function place() {
      if (phone.matches) {
        moved.forEach(function (button) { actions.appendChild(button); });
        if (!actions.parentNode) {
          identity.parentNode.insertBefore(actions, identity.nextSibling);
        }
      } else if (actions.parentNode) {
        moved.forEach(function (button) { toolbar.appendChild(button); });
        actions.parentNode.removeChild(actions);
      }
      if (placeOutline) {
        placeOutline();
      }
    }
    place();
    if (phone.addEventListener) {
      phone.addEventListener("change", place);
    } else if (phone.addListener) {
      phone.addListener(place);
    }
  });
}());
