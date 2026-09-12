// Public marketing site only. See ../ANALYTICS.md for event definitions.
(function () {
  "use strict";
  var config = window.SUGARCLOCK_ANALYTICS;
  if (!config || !/^phc_/.test(config.token) ||
      (config.enabledHosts || []).indexOf(window.location.hostname) === -1) return;

  var queue = [];
  var client;
  var failed = false;
  var page = /faq\.html$/.test(location.pathname) ? "faq" : "home";
  function track(event, properties) {
    var props = Object.assign({
      page: page,
      schema_version: 2,
      web_serial_supported: "serial" in navigator,
      secure_context: window.isSecureContext,
    }, properties);
    try {
      if (client) client.capture(event, props);
      else if (!failed && queue.length < 100) queue.push([event, props]);
    } catch (_) { /* Analytics must never break site interactions. */ }
  }

  var script = document.createElement("script");
  script.async = true;
  script.src = config.assetHost + "/static/array.js";
  script.onerror = function () { failed = true; queue = []; };
  script.onload = function () {
    try {
      window.posthog.init(config.token, {
        api_host: config.apiHost,
        autocapture: false,
        capture_pageview: false,
        capture_pageleave: false,
        capture_dead_clicks: false,
        capture_exceptions: false,
        capture_performance: false,
        capture_heatmaps: false,
        disable_session_recording: true,
        disable_surveys: true,
        person_profiles: "never",
        persistence: "localStorage",
        // Do not persist campaign or referring URL parameters.
        save_campaign_params: false,
        save_referrer: false,
        before_send: function (event) {
          if (!event) return event;
          // SDK attribution can live on the event or in person-property maps.
          // Cover current, initial, and session-entry URLs in every location.
          function sanitize(props) {
            if (!props || typeof props !== "object") return;
            Object.keys(props).forEach(function (key) {
              if (!/^\$.*(?:url|referrer)$/.test(key) || !props[key]) return;
              try {
                var url = new URL(props[key]);
                props[key] = url.origin + url.pathname;
              } catch (_) { delete props[key]; }
            });
          }
          sanitize(event.properties);
          sanitize(event.$set);
          sanitize(event.$set_once);
          if (event.properties) {
            sanitize(event.properties.$set);
            sanitize(event.properties.$set_once);
          }
          return event;
        },
        loaded: function (sdk) {
          client = sdk;
          queue.splice(0).forEach(function (entry) {
            try { sdk.capture(entry[0], entry[1]); } catch (_) {}
          });
        },
      });
    } catch (_) { script.onerror(); }
  };
  document.head.appendChild(script);
  track("$pageview");

  document.addEventListener("click", function (event) {
    var target = event.target;
    var el = target.closest && target.closest("[data-track-event]");
    if (!el) return;
    var props = {};
    // Only static, declared properties; never capture DOM text or input values.
    ["location", "vendor", "method"].forEach(function (key) {
      var value = el.getAttribute("data-track-" + key);
      if (value) props[key.replace(/-/g, "_")] = value;
    });
    if (el.getAttribute("data-track-event") === "demo_video_requested") {
      var videoId = el.getAttribute("data-video-id");
      if (videoId) props.video_id = videoId;
    }
    track(el.getAttribute("data-track-event"), props);
  }, true);

  function wirePage() {
    document.querySelectorAll(".faq-question").forEach(function (button) {
      button.addEventListener("click", function () {
        // Capture phase reads state before the site's accordion toggles it.
        var questionId = button.getAttribute("data-question-id");
        if (questionId && !button.closest(".faq-item").classList.contains("active")) {
          track("faq_answer_opened", { question_id: questionId });
        }
      }, true);
    });
    if (!("IntersectionObserver" in window)) return;
    var observer = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        track("section_viewed", { section: entry.target.id });
        observer.unobserve(entry.target);
      });
    }, { threshold: 0.15 });
    document.querySelectorAll("#why, #process, #install").forEach(function (el) {
      observer.observe(el);
    });
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", wirePage);
  else wirePage();
})();
