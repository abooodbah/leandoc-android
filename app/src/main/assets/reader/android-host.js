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
}());
