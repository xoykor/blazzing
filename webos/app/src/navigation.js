(function (global) {
  "use strict";

  var current = null;

  function focusables() {
    var nodes = document.querySelectorAll(".screen.is-active .focusable:not([disabled])");
    return Array.prototype.slice.call(nodes);
  }

  function center(rect) {
    return {
      x: rect.left + rect.width / 2,
      y: rect.top + rect.height / 2
    };
  }

  function scoreDirection(from, to, dx, dy) {
    var a = center(from.getBoundingClientRect());
    var b = center(to.getBoundingClientRect());
    var vx = b.x - a.x;
    var vy = b.y - a.y;
    var directional = vx * dx + vy * dy;

    if (directional <= 0) {
      return Number.POSITIVE_INFINITY;
    }

    var cross = Math.abs(vx * dy - vy * dx);
    var distance = Math.sqrt(vx * vx + vy * vy);
    return distance + cross * 2.5;
  }

  function setFocus(element) {
    if (!element) {
      return;
    }

    if (current) {
      current.classList.remove("is-focused");
    }

    current = element;
    current.classList.add("is-focused");
    current.focus();
  }

  function focusFirst() {
    var items = focusables();
    setFocus(items.length ? items[0] : null);
  }

  function move(dx, dy) {
    var items = focusables();
    if (!items.length) {
      return;
    }

    if (!current || items.indexOf(current) < 0) {
      setFocus(items[0]);
      return;
    }

    var best = null;
    var bestScore = Number.POSITIVE_INFINITY;

    items.forEach(function (candidate) {
      var score;
      if (candidate === current) {
        return;
      }
      score = scoreDirection(current, candidate, dx, dy);
      if (score < bestScore) {
        best = candidate;
        bestScore = score;
      }
    });

    if (best) {
      setFocus(best);
    }
  }

  document.addEventListener("keydown", function (event) {
    switch (event.keyCode) {
      case 37:
        event.preventDefault();
        move(-1, 0);
        break;
      case 38:
        event.preventDefault();
        move(0, -1);
        break;
      case 39:
        event.preventDefault();
        move(1, 0);
        break;
      case 40:
        event.preventDefault();
        move(0, 1);
        break;
      case 13:
        if (current &&
            (current.tagName !== "INPUT" ||
             current.type === "checkbox" ||
             current.type === "radio")) {
          event.preventDefault();
          current.click();
        }
        break;
      case 461:
        event.preventDefault();
        document.dispatchEvent(new CustomEvent("blazzing-back"));
        break;
      default:
        break;
    }
  });

  document.addEventListener("mouseover", function (event) {
    if (event.target && event.target.classList && event.target.classList.contains("focusable")) {
      setFocus(event.target);
    }
  });

  global.BlazzingNavigation = {
    focusFirst: focusFirst,
    setFocus: setFocus
  };
}(window));
