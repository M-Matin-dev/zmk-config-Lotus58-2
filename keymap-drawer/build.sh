#!/usr/bin/env bash
# Regenerate keymap-drawer/lotus58-keymap.html from config/lotus58.keymap.
# Needs keymap-drawer: pipx install keymap-drawer  (or pip install keymap-drawer)
set -euo pipefail
cd "$(dirname "$0")"
KEYMAP=${KEYMAP:-keymap}
LAYERS=("Win/Lin" "Mac" "Lower" "Raise" "Adjust")

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

"$KEYMAP" -c config.yaml parse -z ../config/lotus58.keymap > lotus58.yaml
for i in "${!LAYERS[@]}"; do
  "$KEYMAP" -c config.yaml draw lotus58.yaml -s "${LAYERS[$i]}" > "$tmp/$i.svg"
done

python3 - "$tmp" "${LAYERS[@]}" <<'EOF'
import html, re, sys
tmp, layers = sys.argv[1], sys.argv[2:]
boards = []
for i, name in enumerate(layers):
    svg = open(f"{tmp}/{i}.svg", encoding="utf-8").read()
    svg = re.sub(r'\s(width|height)="\d+"', '', svg, count=2)  # keep viewBox, let CSS size it
    boards.append(f'<div class="board" data-name="{html.escape(name)}" role="tabpanel">\n{svg}\n</div>')
tpl = open("template.html", encoding="utf-8").read()
open("lotus58-keymap.html", "w", encoding="utf-8").write(tpl.replace("<!--BOARDS-->", "\n".join(boards)))
EOF
echo "wrote $(pwd)/lotus58-keymap.html"
