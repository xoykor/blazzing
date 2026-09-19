(function (global) {
  "use strict";

  function parseAttributes(line) {
    var attrs = {};
    var expression = /([A-Za-z0-9_-]+)="([^"]*)"/g;
    var match;

    while ((match = expression.exec(line)) !== null) {
      attrs[match[1]] = match[2];
    }

    return attrs;
  }

  function resolveUrl(value, baseUrl) {
    var anchor;

    if (/^https?:\/\//i.test(value)) {
      return value;
    }

    if (!baseUrl) {
      return value;
    }

    anchor = document.createElement("a");
    anchor.href = baseUrl;
    anchor.href = value;
    return anchor.href;
  }

  function parse(text, baseUrl) {
    var lines = String(text || "").replace(/\r/g, "").split("\n");
    var pending = null;
    var items = [];
    var groups = [];
    var seenGroups = {};
    var i;
    var line;
    var attrs;
    var comma;
    var title;
    var group;
    var item;

    for (i = 0; i < lines.length; i += 1) {
      line = lines[i].trim();

      if (!line) {
        continue;
      }

      if (line.indexOf("#EXTINF:") === 0) {
        attrs = parseAttributes(line);
        comma = line.lastIndexOf(",");
        title = comma >= 0 ? line.slice(comma + 1).trim() : "";
        pending = {
          title: title || attrs["tvg-name"] || "Sem título",
          group: attrs["group-title"] || "Sem categoria",
          logo: attrs["tvg-logo"] || ""
        };
        continue;
      }

      if (line.charAt(0) === "#") {
        continue;
      }

      if (!pending) {
        pending = {
          title: line,
          group: "Sem categoria",
          logo: ""
        };
      }

      group = pending.group || "Sem categoria";
      item = {
        index: items.length,
        title: pending.title,
        group: group,
        logo: pending.logo,
        url: resolveUrl(line, baseUrl)
      };

      if (/^https?:\/\//i.test(item.url)) {
        items.push(item);
        if (!seenGroups[group]) {
          seenGroups[group] = true;
          groups.push(group);
        }
      }

      pending = null;
    }

    groups.sort(function (a, b) {
      return a.toLowerCase().localeCompare(b.toLowerCase());
    });

    return {
      items: items,
      groups: groups
    };
  }

  global.BlazzingM3U = {
    parse: parse
  };
}(window));
