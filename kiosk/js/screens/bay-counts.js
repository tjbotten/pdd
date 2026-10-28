import { iconSvg } from "../icons.js";

function padCount(value, width) {
  const n = Number.isFinite(value) ? Math.max(0, Math.floor(value)) : 0;
  return String(n).padStart(width ?? 2, "0");
}

export function renderBayCounts(root, config, counts) {
  const rows = (config.bayTypes || [])
    .map((bay) => {
      const value = counts[bay.id];
      const icon = bay.icon
        ? `<div class="bay-icon">${iconSvg(bay.icon, bay.color)}</div>`
        : "";
      return `
        <div class="bay-row" data-bay="${bay.id}">
          <div class="bay-meta">
            ${icon}
            <div class="bay-label">${escapeHtml(bay.label)}</div>
          </div>
          <div class="bay-count" data-count-for="${bay.id}" style="color:${bay.color}">
            ${padCount(value, bay.pad)}
          </div>
        </div>`;
    })
    .join("");

  const logoSrc = config.branding?.logo || "assets/logo.svg";
  const brandName = config.branding?.name || "";

  root.innerHTML = `
    <section class="bay-screen" aria-label="Parking bay availability">
      <header class="bay-header">${escapeHtml(config.levelLabel || config.levelId || "")}</header>
      <div class="bay-rows">${rows}</div>
      <footer class="bay-footer">
        <img class="logo" src="${escapeAttr(logoSrc)}" alt="" />
        <span class="bay-footer-name">${escapeHtml(brandName)}</span>
      </footer>
    </section>`;
}

export function updateBayCounts(root, config, counts, changedIds = null) {
  for (const bay of config.bayTypes || []) {
    const el = root.querySelector(`[data-count-for="${bay.id}"]`);
    if (!el) continue;
    const next = padCount(counts[bay.id], bay.pad);
    if (el.textContent !== next) {
      el.textContent = next;
      if (!changedIds || changedIds.has(bay.id)) {
        el.classList.remove("updated");
        // Force reflow so the animation retriggers
        void el.offsetWidth;
        el.classList.add("updated");
        window.setTimeout(() => el.classList.remove("updated"), 220);
      }
    }
  }
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function escapeAttr(value) {
  return escapeHtml(value).replaceAll("'", "&#39;");
}
