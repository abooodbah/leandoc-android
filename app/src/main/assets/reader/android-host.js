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
