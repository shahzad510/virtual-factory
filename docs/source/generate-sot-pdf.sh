#!/usr/bin/env bash
# Regenerate docs/MES_SCADA_Virtual_Factory_Source_of_Truth.pdf from the Markdown source.
# This script is the only supported way to update the active SoT PDF.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/docs/source/MES_SCADA_Virtual_Factory_Source_of_Truth.md"
CSS="$ROOT/docs/source/sot.css"
HTML="$ROOT/docs/source/_sot_build.html"
PDF="$ROOT/docs/MES_SCADA_Virtual_Factory_Source_of_Truth.pdf"

if [[ ! -f "$SRC" ]]; then
  echo "missing SoT source: $SRC" >&2
  exit 1
fi

pandoc "$SRC" \
  --standalone \
  --from markdown \
  --to html5 \
  --css "$CSS" \
  --metadata title="MES + SCADA + Virtual Factory — Source of Truth" \
  --output "$HTML"

CHROME="$(command -v google-chrome || command -v chromium || command -v chromium-browser || true)"
if [[ -z "$CHROME" ]]; then
  echo "google-chrome / chromium is required to print the SoT PDF" >&2
  exit 1
fi

"$CHROME" \
  --headless \
  --disable-gpu \
  --no-pdf-header-footer \
  --no-sandbox \
  --print-to-pdf="$PDF" \
  "file://$HTML"

rm -f "$HTML"
echo "wrote $PDF"
