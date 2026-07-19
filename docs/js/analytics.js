// SugarClock marketing site analytics (Mixpanel).
//
// Tracking plan:
//   page_viewed            - fires once per page load
//   firmware_flash_started - value moment: visitor clicks the browser-based
//                            "Install SugarClock" (esp-web-tools) button
//   Delegated link events, declared in the markup via data-track-event:
//     youtube_video_played  - clicking the demo video thumbnail
//     github_viewed         - clicking a "View on GitHub" link (data-track-location)
//     purchase_link_clicked - clicking a buy link  (data-track-vendor: amazon|ulanzi)
//
// To track a new element, add data-track-event="event_name" (plus any
// data-track-<prop>="value") to it in the HTML - no JS change needed.
//
// No login exists on this site, so no identify()/reset() and no profiles are
// created for anonymous visitors. Event names are static snake_case.

(function () {
  var MIXPANEL_TOKEN = "50eac6b8979ab86a5efef68b5396f1e7";

  // Official Mixpanel browser loader snippet (mixpanel-2-latest).
  (function (f, b) {
    if (!b.__SV) {
      var e, g, i, h;
      window.mixpanel = b;
      b._i = [];
      b.init = function (e, f, c) {
        function g(a, d) {
          var b = d.split(".");
          2 == b.length && ((a = a[b[0]]), (d = b[1]));
          a[d] = function () {
            a.push([d].concat(Array.prototype.slice.call(arguments, 0)));
          };
        }
        var a = b;
        "undefined" !== typeof c ? (a = b[c] = []) : (c = "mixpanel");
        a.people = a.people || [];
        a.toString = function (a) {
          var d = "mixpanel";
          "mixpanel" !== c && (d += "." + c);
          a || (d += " (stub)");
          return d;
        };
        a.people.toString = function () {
          return a.toString(1) + ".people (stub)";
        };
        i =
          "disable time_event track track_pageview track_links track_forms track_with_groups add_group set_group remove_group register register_once alias unregister identify name_tag set_config reset opt_in_tracking opt_out_tracking has_opted_in_tracking has_opted_out_tracking clear_opt_in_out_tracking start_batch_senders people.set people.set_once people.unset people.increment people.append people.union people.track_charge people.clear_charges people.delete_user people.remove".split(
            " "
          );
        for (h = 0; h < i.length; h++) g(a, i[h]);
        var j = "set set_once union unset remove delete".split(" ");
        a.get_group = function () {
          function b(c) {
            d[c] = function () {
              call2_args = arguments;
              call2 = [c].concat(Array.prototype.slice.call(call2_args, 0));
              a.push([e, call2]);
            };
          }
          for (
            var d = {}, e = ["get_group"].concat(Array.prototype.slice.call(arguments, 0)), c = 0;
            c < j.length;
            c++
          )
            b(j[c]);
          return d;
        };
        b._i.push([e, f, c]);
      };
      b.__SV = 1.2;
      e = f.createElement("script");
      e.type = "text/javascript";
      e.async = !0;
      e.src =
        "undefined" !== typeof MIXPANEL_CUSTOM_LIB_URL
          ? MIXPANEL_CUSTOM_LIB_URL
          : "file:" === f.location.protocol && "//cdn.mxpnl.com/libs/mixpanel-2-latest.min.js".match(/^\/\//)
          ? "https://cdn.mxpnl.com/libs/mixpanel-2-latest.min.js"
          : "//cdn.mxpnl.com/libs/mixpanel-2-latest.min.js";
      g = f.getElementsByTagName("script")[0];
      g.parentNode.insertBefore(e, g);
    }
  })(document, window.mixpanel || []);

  mixpanel.init(MIXPANEL_TOKEN, {
    persistence: "localStorage",
    // We fire page_viewed manually below so we control the properties.
    track_pageview: false,
  });

  // Derive a stable, static page name from the path (no dynamic event names).
  function pageName() {
    var p = window.location.pathname;
    if (/faq\.html$/.test(p)) return "faq";
    if (p === "/" || /index\.html$/.test(p)) return "home";
    return "other";
  }

  mixpanel.track("page_viewed", {
    page: pageName(),
    page_title: document.title,
  });

  // Value moment: browser-based firmware flash. The esp-web-install-button
  // component wraps the visible activate button, so a delegated click on the
  // component captures the intent to flash.
  function wireFlashButton() {
    var installBtn = document.getElementById("install-btn");
    if (!installBtn) return;
    installBtn.addEventListener("click", function () {
      mixpanel.track("firmware_flash_started", {
        page: pageName(),
        method: "web",
      });
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", wireFlashButton);
  } else {
    wireFlashButton();
  }

  // Generic delegated tracking for elements declaring data-track-event.
  // Any additional data-track-<name> attributes become event properties
  // (kebab-case name -> snake_case property). Runs in the capture phase so
  // the event is recorded even for links that navigate.
  function trackProps(el) {
    var props = { page: pageName() };
    for (var i = 0; i < el.attributes.length; i++) {
      var attr = el.attributes[i];
      if (attr.name.indexOf("data-track-") === 0 && attr.name !== "data-track-event") {
        var key = attr.name.slice("data-track-".length).replace(/-/g, "_");
        props[key] = attr.value;
      }
    }
    return props;
  }

  document.addEventListener(
    "click",
    function (e) {
      var el = e.target.closest && e.target.closest("[data-track-event]");
      if (!el) return;
      mixpanel.track(el.getAttribute("data-track-event"), trackProps(el));
    },
    true
  );
})();
