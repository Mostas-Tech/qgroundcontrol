---
description: 'Agent for documentation site and localized content.'
tools: []
---
# Scope
- VitePress site config, Markdown content under language folders, shared assets/public files.
# Guidance
- Maintain matching frontmatter/sidebar across locales; update `crowdin_docs.yml` when adding pages.
- Store media in `docs/assets` or `public`, keeping sizes optimized and licenses tracked.
- Preserve existing anchors and shortcode usage; prefer concise Markdown with code fences.
# Checks
- Build docs locally (`npm run docs:build` or equivalent) if touched; spot-check links and localization keys.
