# LeanDoc for Android site

A static page for GitHub Pages whose job is to sign people up for the closed
test on Google Play: `index.html`, `styles.css`, and the files in
`assets/`. There is no build step, no script, no cookie, and no font or style
from another host. `.github/workflows/pages.yml` deploys this folder from
`main` to https://abooodbah.github.io/leandoc-android/.

## Preview

```sh
python3 -m http.server 8080 --directory site
```

Then open `http://localhost:8080/`.

## Validate

```sh
node site/validate.mjs
```

It checks the page structure, the sign-up order (the testers group button
comes first and the group link comes before the Play tester page), the links
the page must keep, same-page and local links, image sizes and alt text, that
nothing is loaded from another host, and the writing rules. The Pages
workflow runs it on every pull request that touches `site/` and before every
deploy. It does not replace a look in a browser at phone and desktop widths.

## Images

- `assets/leandoc-phone-light.png` and `assets/leandoc-phone-dark.png` are
  `store/screenshots/light.png` and `dark.png` (the Play listing
  screenshots) scaled to 540x960 and reduced to 256 colors.
- `assets/social-preview.png` is `store/feature-graphic.png` (1024x500).
- `assets/leandoc-icon.png` is `store/icon-512.png` scaled to 192x192.
