/** Simple SVG icons matching the mockup style. */

export function iconSvg(name, color) {
  const stroke = color || "currentColor";
  switch (name) {
    case "car":
      return `
        <svg viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
          <path d="M16 40h32l-3.5-12.5a6 6 0 0 0-5.7-4.5H25.2a6 6 0 0 0-5.7 4.5L16 40Z" stroke="${stroke}" stroke-width="3.5" stroke-linejoin="round"/>
          <path d="M20 28h24" stroke="${stroke}" stroke-width="3" stroke-linecap="round"/>
          <circle cx="22" cy="44" r="4.5" stroke="${stroke}" stroke-width="3"/>
          <circle cx="42" cy="44" r="4.5" stroke="${stroke}" stroke-width="3"/>
          <path d="M26 22c2.2-3.5 4.8-5 6-5s3.8 1.5 6 5" stroke="${stroke}" stroke-width="3" stroke-linecap="round"/>
          <path d="M28 34h8" stroke="${stroke}" stroke-width="2.5" stroke-linecap="round"/>
        </svg>`;
    case "accessibility":
      return `
        <svg viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
          <circle cx="32" cy="14" r="5.5" fill="${stroke}"/>
          <path d="M22 26h20" stroke="${stroke}" stroke-width="4" stroke-linecap="round"/>
          <path d="M32 26v10l-8 16" stroke="${stroke}" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>
          <path d="M32 36l10 8" stroke="${stroke}" stroke-width="4" stroke-linecap="round"/>
          <path d="M20 48c2.5 6 7 9 12 9s9.5-3 12-9" stroke="${stroke}" stroke-width="4" stroke-linecap="round"/>
        </svg>`;
    case "ev":
      return `
        <svg viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
          <rect x="18" y="12" width="22" height="36" rx="3" stroke="${stroke}" stroke-width="3.5"/>
          <path d="M26 22h6l-4 10h6l-8 14 2-10h-6l4-14Z" fill="${stroke}"/>
          <path d="M40 20h6a4 4 0 0 1 4 4v10" stroke="${stroke}" stroke-width="3.5" stroke-linecap="round"/>
          <circle cx="50" cy="38" r="3.5" fill="${stroke}"/>
          <path d="M24 52h10" stroke="${stroke}" stroke-width="3.5" stroke-linecap="round"/>
        </svg>`;
    default:
      return "";
  }
}
