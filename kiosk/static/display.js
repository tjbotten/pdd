(function () {
  var TRAFFIC_MS = 30 * 60 * 1000;
  var WEATHER_MS = 600000;
  var NEWS_MS = 180000;
  var POST_MS = 30000;
  var ROSEBANK = { latitude: -26.146, longitude: 28.041, zoom: 12 };

  var lastTraffic = null;

  function el(tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text != null) node.textContent = text;
    return node;
  }

  function clear(node) {
    while (node.firstChild) node.removeChild(node.firstChild);
  }

  function svgEl(name) {
    return document.createElementNS("http://www.w3.org/2000/svg", name);
  }

  function fit() {
    var board = document.querySelector(".board");
    var scale = Math.min(window.innerWidth / 1080, window.innerHeight / 1920);
    if (!isFinite(scale) || scale <= 0) scale = 1;
    board.style.transform = "scale(" + scale + ")";
    board.style.left = ((window.innerWidth - 1080 * scale) / 2) + "px";
    board.style.top = ((window.innerHeight - 1920 * scale) / 2) + "px";
    if (lastTraffic) drawMap(lastTraffic);
  }

  function worldPixel(lat, lon, zoom) {
    var scale = 256 * Math.pow(2, zoom);
    var sine = Math.sin(lat * Math.PI / 180);
    sine = Math.min(0.9999, Math.max(-0.9999, sine));
    return {
      x: (lon + 180) / 360 * scale,
      y: (0.5 - Math.log((1 + sine) / (1 - sine)) / (4 * Math.PI)) * scale
    };
  }

  function tileImage(kind, zoom, x, y, left, top) {
    var img = document.createElement("img");
    img.className = "tile tile-" + kind;
    img.alt = "";
    img.draggable = false;
    img.style.left = Math.round(left) + "px";
    img.style.top = Math.round(top) + "px";
    img.addEventListener("error", function () {
      img.style.visibility = "hidden";
    });
    img.src = "/tiles/" + kind + "/" + zoom + "/" + x + "/" + y + ".png";
    return img;
  }

  function drawMap(payload) {
    var map = document.getElementById("traffic-map");
    if (!map) return;
    var width = map.clientWidth;
    var height = map.clientHeight;
    if (width < 64 || height < 64) return;
    var center = payload && payload.center ? payload.center : ROSEBANK;
    var lat = typeof center.latitude === "number" ? center.latitude : ROSEBANK.latitude;
    var lon = typeof center.longitude === "number" ? center.longitude : ROSEBANK.longitude;
    var zoom = payload && payload.zoom ? payload.zoom : ROSEBANK.zoom;
    var point = worldPixel(lat, lon, zoom);
    var originX = point.x - width / 2;
    var originY = point.y - height / 2;
    var x0 = Math.floor(originX / 256);
    var y0 = Math.floor(originY / 256);
    var x1 = Math.floor((originX + width - 1) / 256);
    var y1 = Math.floor((originY + height - 1) / 256);
    var colours = !!(payload && payload.colours);
    clear(map);
    var limit = Math.pow(2, zoom);
    for (var ty = y0; ty <= y1; ty++) {
      for (var tx = x0; tx <= x1; tx++) {
        if (tx < 0 || ty < 0 || tx >= limit || ty >= limit) continue;
        var left = tx * 256 - originX;
        var top = ty * 256 - originY;
        map.appendChild(tileImage("base", zoom, tx, ty, left, top));
        if (colours) map.appendChild(tileImage("flow", zoom, tx, ty, left, top));
      }
    }
    map.appendChild(el("p", "map-credit", "© OpenStreetMap"));
  }

  function renderTraffic(payload) {
    lastTraffic = payload || { colours: false, incidents: [], message: "Traffic colours unavailable" };
    drawMap(lastTraffic);
    var legend = document.getElementById("traffic-legend");
    var notes = document.getElementById("traffic-notes");
    clear(legend);
    clear(notes);
    if (lastTraffic.colours) {
      legend.appendChild(legendLine("swatch-light", "Light"));
      legend.appendChild(legendLine("swatch-moderate", "Moderate"));
      legend.appendChild(legendLine("swatch-heavy", "Heavy"));
    } else {
      legend.appendChild(el("p", "status", lastTraffic.message || "Traffic colours unavailable"));
    }
    var incidents = Array.isArray(lastTraffic.incidents) ? lastTraffic.incidents : [];
    var shown = [];
    for (var i = 0; i < incidents.length && shown.length < 4; i++) {
      if (incidents[i] && typeof incidents[i].text === "string" && incidents[i].text) {
        shown.push(incidents[i].text);
      }
    }
    if (shown.length) {
      var list = el("ul", "incident-list");
      for (var n = 0; n < shown.length; n++) list.appendChild(el("li", null, shown[n]));
      notes.appendChild(list);
    }
  }

  function legendLine(swatchClass, label) {
    var row = el("p");
    var swatch = el("span", "swatch " + swatchClass);
    row.appendChild(swatch);
    row.appendChild(document.createTextNode(label));
    return row;
  }

  function iconKind(code) {
    if (typeof code !== "number") return "cloud";
    if (code <= 1) return "sun";
    if (code === 2) return "partly";
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95) return "rain";
    if (code >= 71 && code <= 86) return "rain";
    return "cloud";
  }

  function weatherIcon(code, size) {
    var svg = svgEl("svg");
    svg.setAttribute("viewBox", "0 0 64 64");
    svg.setAttribute("width", String(size));
    svg.setAttribute("height", String(size));
    svg.setAttribute("aria-hidden", "true");
    var kind = iconKind(code);
    if (kind === "sun" || kind === "partly") {
      var sun = svgEl("circle");
      sun.setAttribute("cx", kind === "partly" ? "22" : "32");
      sun.setAttribute("cy", kind === "partly" ? "22" : "32");
      sun.setAttribute("r", "12");
      sun.setAttribute("fill", "#ffe14a");
      svg.appendChild(sun);
    }
    if (kind !== "sun") {
      var cloud = svgEl("ellipse");
      cloud.setAttribute("cx", "36");
      cloud.setAttribute("cy", "38");
      cloud.setAttribute("rx", "18");
      cloud.setAttribute("ry", "12");
      cloud.setAttribute("fill", "#ffffff");
      svg.appendChild(cloud);
    }
    if (kind === "rain") {
      for (var i = 0; i < 3; i++) {
        var drop = svgEl("line");
        drop.setAttribute("x1", String(24 + i * 10));
        drop.setAttribute("y1", "52");
        drop.setAttribute("x2", String(20 + i * 10));
        drop.setAttribute("y2", "62");
        drop.setAttribute("stroke", "#d6ebff");
        drop.setAttribute("stroke-width", "3");
        drop.setAttribute("stroke-linecap", "round");
        svg.appendChild(drop);
      }
    }
    return svg;
  }

  function calendarDate(iso) {
    if (!iso || !/^\d{4}-\d{2}-\d{2}$/.test(iso)) return null;
    var parts = iso.split("-");
    return new Date(Date.UTC(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]), 12, 0, 0));
  }

  function formatDay(iso) {
    var date = calendarDate(iso);
    if (!date) return "";
    try {
      return new Intl.DateTimeFormat("en-GB", {
        timeZone: "UTC",
        weekday: "short",
        day: "numeric",
        month: "short"
      }).format(date);
    } catch (error) {
      return iso;
    }
  }

  function weekday(iso) {
    var date = calendarDate(iso);
    if (!date) return "";
    try {
      return new Intl.DateTimeFormat("en-GB", { timeZone: "UTC", weekday: "short" }).format(date);
    } catch (error) {
      return "";
    }
  }

  function renderWeather(payload) {
    var body = document.getElementById("weather-body");
    clear(body);
    var places = payload && Array.isArray(payload.places) ? payload.places : [];
    if (!places.length) {
      body.appendChild(el("p", "status", "Weather unavailable"));
      return;
    }
    for (var i = 0; i < places.length; i++) {
      body.appendChild(cityBlock(places[i], i === 0));
    }
  }

  function cityBlock(place, primary) {
    var block = el("div", primary ? "city" : "city city-secondary");
    block.appendChild(el("p", "city-name", place.name || ""));
    if (!place.available || typeof place.temperature !== "number") {
      block.appendChild(el("p", "status", "Weather unavailable"));
      return block;
    }
    var now = el("div", "now");
    now.appendChild(weatherIcon(place.code, primary ? 92 : 68));
    now.appendChild(el("div", "temp", place.temperature + "°"));
    if (place.condition) now.appendChild(el("div", "condition", place.condition));
    block.appendChild(now);
    var days = Array.isArray(place.days) ? place.days : [];
    var today = days.length ? days[0] : null;
    if (today && today.date) block.appendChild(el("p", "meta", formatDay(today.date)));
    if (today && typeof today.high === "number" && typeof today.low === "number") {
      var range = el("div", "highlow");
      range.appendChild(el("span", null, "High " + today.high + "°"));
      range.appendChild(el("span", null, "Low " + today.low + "°"));
      block.appendChild(range);
    }
    var upcoming = el("div", "days");
    var count = 0;
    for (var i = 1; i < days.length && count < 3; i++) {
      if (typeof days[i].high !== "number") continue;
      upcoming.appendChild(dayCell(days[i]));
      count++;
    }
    if (count) block.appendChild(upcoming);
    return block;
  }

  function dayCell(day) {
    var cell = el("div", "day");
    var name = weekday(day.date) || day.label || "";
    if (name) cell.appendChild(el("span", "day-name", name));
    cell.appendChild(weatherIcon(day.code, 36));
    cell.appendChild(el("span", "day-high", day.high + "°"));
    if (typeof day.low === "number") cell.appendChild(el("span", "day-low", day.low + "°"));
    return cell;
  }

  function renderPosts(payload) {
    var body = document.getElementById("announcement-body");
    clear(body);
    if (!payload || !Array.isArray(payload.posts)) {
      body.appendChild(el("p", "status", "Announcements unavailable"));
      return;
    }
    if (!payload.posts.length) {
      body.appendChild(el("p", "status", "No announcements"));
      return;
    }
    var list = el("ul", "bullets");
    var limit = Math.min(payload.posts.length, 5);
    for (var i = 0; i < limit; i++) {
      if (payload.posts[i] && payload.posts[i].body) list.appendChild(el("li", null, payload.posts[i].body));
    }
    body.appendChild(list);
  }

  function renderNews(payload) {
    var body = document.getElementById("news-body");
    clear(body);
    var stories = payload && Array.isArray(payload.stories) ? payload.stories : [];
    if (!stories.length) {
      body.appendChild(el("p", "status", (payload && payload.message) || "News unavailable"));
      return;
    }
    var list = el("ul", "headlines");
    var limit = Math.min(stories.length, 4);
    for (var i = 0; i < limit; i++) {
      var story = stories[i];
      if (!story || !story.title) continue;
      var item = el("li");
      if (story.source) item.appendChild(el("span", "source", story.source));
      item.appendChild(el("p", "headline", "\u201c" + story.title + "\u201d"));
      list.appendChild(item);
    }
    body.appendChild(list);
  }

  function johannesburgDay(date) {
    try {
      return new Intl.DateTimeFormat("en-GB", {
        timeZone: "Africa/Johannesburg",
        day: "numeric"
      }).format(date);
    } catch (error) {
      return "";
    }
  }

  function paintCalendar() {
    var node = document.getElementById("calendar-day");
    if (!node) return;
    var day = johannesburgDay(new Date());
    if (day) node.textContent = day;
  }

  function load(url, render) {
    fetch(url, { cache: "no-store" })
      .then(function (response) {
        if (!response.ok) throw new Error("bad status");
        return response.json();
      })
      .then(render)
      .catch(function () {
        render(null);
      });
  }

  function refreshTraffic() { load("/api/traffic", renderTraffic); }
  function refreshWeather() { load("/api/weather", renderWeather); }
  function refreshNews() { load("/api/news", renderNews); }
  function refreshPosts() { load("/api/announcements", renderPosts); }

  fit();
  paintCalendar();
  window.addEventListener("resize", fit);
  refreshTraffic();
  refreshWeather();
  refreshNews();
  refreshPosts();
  setInterval(refreshTraffic, TRAFFIC_MS);
  setInterval(refreshWeather, WEATHER_MS);
  setInterval(refreshNews, NEWS_MS);
  setInterval(refreshPosts, POST_MS);
  setInterval(paintCalendar, 60000);
  requestAnimationFrame(function () {
    if (lastTraffic) drawMap(lastTraffic);
  });
})();
