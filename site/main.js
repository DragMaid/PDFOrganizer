// Progressive enhancement only: every link works without this file.
(() => {
  "use strict";

  const REPO = "DragMaid/PDFOrganizer";
  const PLATFORMS = {
    linux: { label: "Linux", suffix: "linux-x86_64.AppImage" },
    mac: { label: "macOS", suffix: "macos-universal.dmg" },
    windows: { label: "Windows", suffix: "windows-x64.zip" },
  };

  // ── Which desktop is this? ────────────────────────────────────────────────
  function detectPlatform() {
    const hint = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || "";
    const ua = navigator.userAgent || "";
    const s = `${hint} ${ua}`.toLowerCase();
    if (/android|iphone|ipad|ipod/.test(s)) return null; // no mobile build
    if (s.includes("win")) return "windows";
    if (s.includes("mac")) return "mac";
    if (s.includes("linux") || s.includes("x11")) return "linux";
    return null;
  }

  const platform = detectPlatform();
  const hero = document.querySelector('[data-download="auto"]');
  const heroLabel = document.querySelector("[data-download-label]");

  if (platform) {
    heroLabel.textContent = `Download for ${PLATFORMS[platform].label}`;
    const row = document.querySelector(`[data-platform="${platform}"]`);
    if (row) row.classList.add("is-yours");
  } else {
    heroLabel.textContent = "Download";
    hero.setAttribute("href", "#download");
  }

  // ── Point the buttons at real files from the newest release ───────────────
  // Includes prereleases (the project ships them), so /releases rather than /releases/latest.
  fetch(`https://api.github.com/repos/${REPO}/releases?per_page=5`, {
    headers: { Accept: "application/vnd.github+json" },
  })
    .then((r) => (r.ok ? r.json() : Promise.reject(r.status)))
    .then((releases) => {
      const release = releases.find((r) => !r.draft && r.assets && r.assets.length);
      if (!release) return;

      const assetFor = (suffix) => release.assets.find((a) => a.name.endsWith(suffix));

      document.querySelectorAll("[data-asset]").forEach((link) => {
        const asset = assetFor(link.dataset.asset);
        if (asset) link.href = asset.browser_download_url;
      });

      if (platform) {
        const asset = assetFor(PLATFORMS[platform].suffix);
        if (asset) hero.href = asset.browser_download_url;
      }

      const version = release.tag_name;
      const date = new Date(release.published_at).toLocaleDateString(undefined, {
        year: "numeric", month: "short", day: "numeric",
      });
      const meta = document.querySelector("[data-release-meta]");
      if (meta) meta.textContent = `${version}${release.prerelease ? " (pre-release)" : ""} · ${date} · Free and open source`;
      const lede = document.querySelector("[data-release-version]");
      if (lede) lede.textContent = `Latest: ${version}, released ${date}.`;
    })
    .catch(() => { /* rate-limited or offline: the Releases page links stay */ });

  // ── Screenshot tour ───────────────────────────────────────────────────────
  const shot = document.querySelector("[data-tour]");
  if (shot) {
    const spot = shot.querySelector("[data-spot]");
    const stops = [...shot.querySelectorAll(".tour__stop")];
    let active = null;

    const show = (stop) => {
      const [x, y, w, h] = stop.dataset.region.split(",").map(Number);
      const pad = 0.6;
      spot.style.setProperty("--x", `${Math.max(0, x - pad)}%`);
      spot.style.setProperty("--y", `${Math.max(0, y - pad)}%`);
      spot.style.setProperty("--w", `${w + pad * 2}%`);
      spot.style.setProperty("--h", `${h + pad * 2}%`);
      shot.classList.add("is-touring");
      stops.forEach((s) => s.setAttribute("aria-pressed", String(s === stop)));
      active = stop;
    };
    const clear = () => {
      shot.classList.remove("is-touring");
      stops.forEach((s) => s.setAttribute("aria-pressed", "false"));
      active = null;
    };

    stops.forEach((stop) => {
      stop.setAttribute("aria-pressed", "false");
      stop.addEventListener("mouseenter", () => show(stop));
      stop.addEventListener("focus", () => show(stop));
      stop.addEventListener("click", () => (active === stop ? clear() : show(stop)));
    });
    shot.querySelector(".tour").addEventListener("mouseleave", () => {
      if (!shot.contains(document.activeElement) || document.activeElement === document.body) clear();
    });
    shot.addEventListener("focusout", (e) => { if (!shot.contains(e.relatedTarget)) clear(); });
    document.addEventListener("keydown", (e) => { if (e.key === "Escape" && active) clear(); });
  }

  // ── Copy buttons ──────────────────────────────────────────────────────────
  document.querySelectorAll("[data-copy]").forEach((btn) => {
    const label = btn.querySelector("span");
    btn.addEventListener("click", async () => {
      const text = document.getElementById(btn.dataset.copy).innerText
        .split("\n").map((l) => l.replace(/\s+#.*$/, "")).filter((l) => l && !l.startsWith("#")).join("\n");
      try {
        await navigator.clipboard.writeText(text);
        btn.classList.add("is-done");
        label.textContent = "Copied";
      } catch {
        label.textContent = "Select and copy";
      }
      setTimeout(() => { btn.classList.remove("is-done"); label.textContent = "Copy"; }, 1800);
    });
  });

  // ── Top bar hairline once the page moves ──────────────────────────────────
  const bar = document.querySelector(".topbar");
  const onScroll = () => bar.classList.toggle("is-scrolled", window.scrollY > 8);
  onScroll();
  window.addEventListener("scroll", onScroll, { passive: true });
})();
