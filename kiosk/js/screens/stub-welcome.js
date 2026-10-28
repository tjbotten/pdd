/** Placeholder for future welcome / info layouts. */

export function renderStubScreen(root, { title, body }) {
  root.innerHTML = `
    <section class="stub-screen" aria-label="${escapeHtml(title)}">
      <h1>${escapeHtml(title)}</h1>
      <p>${escapeHtml(body)}</p>
    </section>`;
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}
