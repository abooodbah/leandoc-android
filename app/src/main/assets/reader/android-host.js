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

  function receive(event) {
    if (window.LeanDocHost && typeof event.data === "string") {
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
  // Below 760 px the theme, other-app and Open buttons move up beside the
  // file name, and the find field, the view switch and the zoom share the
  // second row. The buttons keep their ids and handlers; they move back into
  // the toolbar when the page is wider again, as in landscape on a tablet.
  var phoneStyle = document.createElement("style");
  phoneStyle.textContent =
    "@media (max-width: 760px) {" +
    " .topbar { flex-wrap: wrap; gap: 6px; padding: 6px 12px; }" +
    " .wordmark { display: none; }" +
    " .identity { flex: 1 1 0; min-width: 0; }" +
    " .file-copy, .file-copy strong { min-width: 0; max-width: 100%; }" +
    " .phone-actions { display: flex; flex: 0 0 auto; align-items: center; gap: 4px; }" +
    " .phone-actions .button { padding-right: 10px; padding-left: 10px; }" +
    " .toolbar { width: 100%; flex-wrap: wrap; gap: 4px; }" +
    " .search-field { flex: 1 1 0; width: auto; min-width: 88px; }" +
    " #findCount:empty { min-width: 0; padding-right: 0; }" +
    " .segmented .segment { min-width: 0; padding-right: 8px; padding-left: 8px; }" +
    " .zoom-step { width: 28px; }" +
    " .zoom-value { min-width: 44px; }" +
    "}";
  document.head.appendChild(phoneStyle);

  document.addEventListener("DOMContentLoaded", function () {
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
    }
    place();
    if (phone.addEventListener) {
      phone.addEventListener("change", place);
    } else if (phone.addListener) {
      phone.addListener(place);
    }
  });
}());
