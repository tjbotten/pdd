(function () {
  var TRAFFIC_MS = 30 * 60 * 1000;
  var WEATHER_MS = 600000;
  var NEWS_MS = 180000;
  var POST_MS = 30000;
  var STORY_MS = 18000;

  var FALLBACK_ROUTES = [
    { from: "Rosebank", to: "Sandton", available: false },
    { from: "Rosebank", to: "Centurion", available: false },
    { from: "Rosebank", to: "Roodepoort", available: false }
  ];

  var newsStories = [];
  var newsIndex = 0;
  var newsTimer = null;
  var newsCycleMs = STORY_MS;
  var posts = [];
  var postIndex = 0;
  var postTimer = null;
  var postsOk = true;

  function el(tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text != null) node.textContent = text;
    return node;
  }

  function clear(node) {
    while (node.firstChild) node.removeChild(node.firstChild);
  }

  function johannesburgTime(date) {
    try {
      return new Intl.DateTimeFormat("en-GB", {
        timeZone: "Africa/Johannesburg",
        hour: "2-digit",
        minute: "2-digit",
        hourCycle: "h23"
      }).format(date);
    } catch (error) {
      return "";
    }
  }

  function tickClock() {
    var node = document.getElementById("clock");
    var text = johannesburgTime(new Date());
    node.textContent = text || "--:--";
  }

  function fit() {
    var board = document.querySelector(".board");
    var scale = Math.min(window.innerWidth / 1080, window.innerHeight / 1920);
    if (!isFinite(scale) || scale <= 0) scale = 1;
    board.style.transform = "scale(" + scale + ")";
    board.style.left = ((window.innerWidth - 1080 * scale) / 2) + "px";
    board.style.top = ((window.innerHeight - 1920 * scale) / 2) + "px";
  }

  function minutes(seconds) {
    if (typeof seconds !== "number" || !isFinite(seconds) || seconds < 0) return null;
    var value = Math.round(seconds / 60);
    return value < 1 ? 1 : value;
  }

  function delayPhrase(route) {
    var live = route.liveSeconds;
    var typical = route.typicalSeconds;
    var freeFlow = route.freeFlowSeconds;
    var hasTypical = typeof typical === "number";
    var baseline = hasTypical ? typical : freeFlow;
    var name = hasTypical ? "typical" : "free-flow";
    if (typeof live !== "number" || typeof baseline !== "number") return null;
    var deltaMin = Math.round((live - baseline) / 60);
    var level = "ok";
    var text;
    if (Math.abs(live - baseline) < 60) {
      text = "On time vs " + name;
    } else if (deltaMin > 0) {
      text = "+" + deltaMin + " min vs " + name;
      level = deltaMin >= 10 ? "bad" : "slow";
    } else {
      text = deltaMin + " min vs " + name;
    }
    var parts = [];
    if (typeof route.distanceMeters === "number") {
      parts.push((route.distanceMeters / 1000).toFixed(1) + " km");
    }
    parts.push(text);
    if (hasTypical && typeof freeFlow === "number") {
      var freeMin = minutes(freeFlow);
      if (freeMin != null) parts.push("free-flow " + freeMin + " min");
    }
    return { text: parts.join(" · "), level: level };
  }

  function renderTraffic(payload) {
    var body = document.getElementById("traffic-body");
    clear(body);
    var routes = payload && payload.routes && payload.routes.length ? payload.routes : FALLBACK_ROUTES;
    var list = el("div", "routes");
    for (var i = 0; i < routes.length; i++) {
      var route = routes[i];
      var row = el("div", "route");
      if (route.id) row.id = "route-" + route.id;
      var name = el("div", "route-name", route.from + " → " + route.to);
      if (!route.available) name.classList.add("route-name-solo");
      row.appendChild(name);
      if (route.available) {
        var mins = minutes(route.liveSeconds);
        row.appendChild(el("div", "route-time", mins == null ? "Unavailable" : mins + " min"));
        var phrase = delayPhrase(route);
        if (phrase) row.appendChild(el("div", "route-meta " + phrase.level, phrase.text));
      } else {
        var missing = el("div", "route-time missing", "Unavailable");
        row.appendChild(missing);
      }
      list.appendChild(row);
    }
    body.appendChild(list);
    if (payload && payload.message) {
      body.appendChild(el("p", "note", payload.message));
    }
  }

  function renderWeather(payload) {
    var body = document.getElementById("weather-body");
    clear(body);
    var places = payload && payload.places && payload.places.length ? payload.places : [
      { name: "Johannesburg", available: false },
      { name: "Pretoria", available: false }
    ];
    if (payload && !payload.available && payload.message && places.every(function (place) { return !place.available; })) {
      var failed = el("div", "wx-grid");
      for (var n = 0; n < places.length; n++) {
        var blank = el("article", "wx");
        blank.appendChild(el("h2", null, places[n].name));
        blank.appendChild(el("p", "status", "Unavailable"));
        failed.appendChild(blank);
      }
      body.appendChild(failed);
      return;
    }
    var grid = el("div", "wx-grid");
    for (var i = 0; i < places.length; i++) {
      grid.appendChild(renderPlace(places[i]));
    }
    body.appendChild(grid);
  }

  function renderPlace(place) {
    var card = el("article", "wx");
    card.appendChild(el("h2", null, place.name));
    if (!place.available) {
      card.appendChild(el("p", "status", "Unavailable"));
      return card;
    }
    card.appendChild(el("p", "temp", place.temperature + "°"));
    var condParts = [];
    if (place.condition) condParts.push(place.condition);
    if (typeof place.windKmh === "number") condParts.push(place.windKmh + " km/h");
    if (condParts.length) card.appendChild(el("p", "cond", condParts.join(" · ")));
    var days = place.days || [];
    if (days.length) {
      var dayBlock = el("div", "days");
      for (var d = 0; d < days.length; d++) {
        dayBlock.appendChild(el("span", null, days[d].label + " " + days[d].high + "° / " + days[d].low + "°"));
      }
      card.appendChild(dayBlock);
    }
    var hours = place.hours || [];
    if (hours.length) {
      var row = el("div", "hours");
      for (var h = 0; h < hours.length; h++) {
        var cell = el("div");
        cell.appendChild(el("span", null, hours[h].time));
        cell.appendChild(el("strong", null, hours[h].temperature + "°"));
        if (typeof hours[h].rainPercent === "number" && hours[h].rainPercent >= 30) {
          cell.appendChild(el("em", null, hours[h].rainPercent + "%"));
        }
        row.appendChild(cell);
      }
      card.appendChild(row);
    }
    return card;
  }

  function restartProgress() {
    var bar = document.getElementById("news-progress");
    bar.style.animation = "none";
    void bar.offsetWidth;
    if (newsStories.length < 2) {
      bar.style.width = "0";
      return;
    }
    bar.style.animation = "fill " + (newsCycleMs / 1000) + "s linear";
  }

  function renderStory() {
    var body = document.getElementById("news-body");
    var section = document.getElementById("band-news");
    clear(body);
    section.classList.remove("source-ewn", "source-sky");
    if (!newsStories.length) {
      body.appendChild(el("p", "kicker", "News"));
      body.appendChild(el("p", "status", "News unavailable"));
      restartProgress();
      return;
    }
    var story = newsStories[newsIndex % newsStories.length];
    if (story.source === "Sky News") section.classList.add("source-sky");
    else section.classList.add("source-ewn");
    var label = story.source + (story.kicker ? "  ·  " + story.kicker : "");
    body.appendChild(el("p", "kicker", label));
    body.appendChild(el("h2", "headline", story.title));
    if (story.summary) body.appendChild(el("p", "deck", story.summary));
    restartProgress();
  }

  function advanceStory() {
    if (newsStories.length < 2) return;
    newsIndex = (newsIndex + 1) % newsStories.length;
    renderStory();
  }

  function applyNews(payload) {
    var next = payload && payload.stories ? payload.stories : [];
    var current = newsStories.length ? newsStories[newsIndex % newsStories.length] : null;
    newsStories = next;
    newsIndex = 0;
    if (current && current.link) {
      for (var i = 0; i < next.length; i++) {
        if (next[i].link && next[i].link === current.link) {
          newsIndex = i;
          break;
        }
      }
    }
    if (payload && typeof payload.cycleSeconds === "number" && payload.cycleSeconds >= 15 && payload.cycleSeconds <= 20) {
      newsCycleMs = payload.cycleSeconds * 1000;
    }
    renderStory();
    if (newsTimer) clearInterval(newsTimer);
    newsTimer = setInterval(advanceStory, newsCycleMs);
  }

  function renderPosts() {
    var body = document.getElementById("announcement-body");
    clear(body);
    body.appendChild(el("p", "kicker", "Announcements"));
    if (!postsOk) {
      body.appendChild(el("p", "status", "Announcements unavailable"));
      return;
    }
    if (!posts.length) {
      body.appendChild(el("p", "status", "No announcements"));
      return;
    }
    var post = posts[postIndex % posts.length];
    body.appendChild(el("h2", "headline announcement-text", post.body));
    var bits = [];
    if (posts.length > 1) bits.push((postIndex + 1) + " of " + posts.length);
    var posted = post.createdAt ? johannesburgTime(new Date(post.createdAt)) : "";
    if (posted) bits.push("Posted " + posted);
    if (bits.length) body.appendChild(el("p", "deck", bits.join("  ·  ")));
  }

  function advancePost() {
    if (posts.length < 2) return;
    postIndex = (postIndex + 1) % posts.length;
    renderPosts();
  }

  function applyPosts(payload) {
    postsOk = !payload || payload.available !== false;
    var next = payload && payload.posts ? payload.posts : [];
    var current = posts.length ? posts[postIndex % posts.length] : null;
    posts = next;
    postIndex = 0;
    if (current && current.id != null) {
      for (var i = 0; i < next.length; i++) {
        if (next[i].id === current.id) {
          postIndex = i;
          break;
        }
      }
    }
    renderPosts();
    if (postTimer) clearInterval(postTimer);
    postTimer = setInterval(advancePost, 16000);
  }

  function load(url, onData, onFail) {
    fetch(url, { cache: "no-store" })
      .then(function (response) {
        if (!response.ok) throw new Error("status");
        return response.json();
      })
      .then(onData)
      .catch(onFail);
  }

  function loadTraffic() {
    load("/api/traffic", renderTraffic, function () {
      renderTraffic({ message: "Travel times unavailable", routes: FALLBACK_ROUTES });
    });
  }

  function loadWeather() {
    load("/api/weather", renderWeather, function () {
      renderWeather({
        available: false,
        message: "Weather unavailable",
        places: [
          { name: "Johannesburg", available: false },
          { name: "Pretoria", available: false }
        ]
      });
    });
  }

  function loadNews() {
    load("/api/news", applyNews, function () {
      applyNews({ stories: [] });
    });
  }

  function loadPosts() {
    load("/api/announcements", applyPosts, function () {
      applyPosts({ available: false, posts: [] });
    });
  }

  document.addEventListener("contextmenu", function (event) { event.preventDefault(); });
  window.addEventListener("resize", fit);
  fit();
  tickClock();
  setInterval(tickClock, 15000);
  loadTraffic();
  loadWeather();
  loadNews();
  loadPosts();
  setInterval(loadTraffic, TRAFFIC_MS);
  setInterval(loadWeather, WEATHER_MS);
  setInterval(loadNews, NEWS_MS);
  setInterval(loadPosts, POST_MS);
})();
