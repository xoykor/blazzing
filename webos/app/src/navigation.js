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

  function dispatchBoundary(dx, dy) {
    document.dispatchEvent(new CustomEvent("blazzing-navigation-boundary", {
      detail: {
        dx: dx,
        dy: dy,
        origin: current
      }
    }));
  }

  function catalogWindowBoundary(dx, dy) {
    var grid;
    var cards;
    var i;
    var candidateExists = false;
    var attribute;

    if (!current ||
        dx !== 0 ||
        dy === 0 ||
        !current.classList ||
        !current.classList.contains("media-card")) {
      return false;
    }

    grid = current.parentNode;
    if (!grid || grid.id !== "catalog-items") {
      return false;
    }

    cards = grid.querySelectorAll(".media-card.focusable:not([disabled])");
    for (i = 0; i < cards.length; i += 1) {
      if (cards[i] !== current &&
          scoreDirection(current, cards[i], 0, dy) !==
            Number.POSITIVE_INFINITY) {
        candidateExists = true;
        break;
      }
    }

    if (candidateExists) {
      return false;
    }

    attribute = dy < 0 ?
      "data-has-prev-window" :
      "data-has-next-window";

    if (grid.getAttribute(attribute) !== "true") {
      return false;
    }

    dispatchBoundary(dx, dy);
    return true;
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

    if (catalogWindowBoundary(dx, dy)) {
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
      return;
    }

    document.dispatchEvent(new CustomEvent("blazzing-navigation-boundary", {
      detail: {
        dx: dx,
        dy: dy,
        origin: current
      }
    }));
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

  function focusableAncestor(node) {
    while (node && node !== document) {
      if (node.classList && node.classList.contains("focusable")) {
        return node;
      }
      node = node.parentNode;
    }

    return null;
  }

  document.addEventListener("mouseover", function (event) {
    var target = focusableAncestor(event.target);

    if (target) {
      setFocus(target);
    }
  });

  global.BlazzingNavigation = {
    focusFirst: focusFirst,
    setFocus: setFocus
  };
}(window));
