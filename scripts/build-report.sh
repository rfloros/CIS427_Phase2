#!/usr/bin/env bash
# Builds docs/phase3-technical-report.html and .pdf from docs/report/*.html
# parts and the revised Curiosity Report, then renders every diagram.
# Needs LibreOffice (soffice) and rsvg-convert; nothing else.
# Usage: bash scripts/build-report.sh
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root/docs" || exit 1

for name in architecture sequence; do
    rsvg-convert -w 1800 "images/${name}_diagram.svg" -o "images/${name}_diagram.png" || exit 1
    rsvg-convert -f pdf "images/${name}_diagram.svg" -o "images/${name}_diagram.pdf" || exit 1
done

# Replaces each <!--MARKER--> line in the template with its fragment file.
cp report/template.html phase3-technical-report.html
for part in CURIOSITY:curiosity-report-revised.html TESTRESULTS:report/test-results.html \
            SCALABILITY:report/scalability.html; do
    marker="${part%%:*}"
    fragment="${part#*:}"
    awk -v marker="<!--$marker-->" -v file="$fragment" \
        '$0 == marker { while ((getline line < file) > 0) print line; next } { print }' \
        phase3-technical-report.html > phase3-technical-report.tmp \
        && mv phase3-technical-report.tmp phase3-technical-report.html || exit 1
done

# LibreOffice does not load linked images when it imports HTML, so the PDF is
# rendered from a temporary copy with the diagrams embedded as data URIs.
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
python3 -I - "$scratch/phase3-technical-report.html" <<'PY' || exit 1
import base64, re, sys
html = open("phase3-technical-report.html").read()
def embed(match):
    data = base64.b64encode(open(match.group(1), "rb").read()).decode()
    return 'src="data:image/png;base64,' + data + '"'
open(sys.argv[1], "w").write(re.sub(r'src="(images/[a-z_]+\.png)"', embed, html))
PY
soffice --headless --convert-to pdf --outdir "$scratch" "$scratch/phase3-technical-report.html" > /dev/null 2>&1 \
    || { echo "soffice failed to render the PDF"; exit 1; }
cp "$scratch/phase3-technical-report.pdf" phase3-technical-report.pdf
pdfinfo phase3-technical-report.pdf | grep -E "^Pages"
pdfimages -list phase3-technical-report.pdf | grep -c " image " | sed 's/^/embedded images: /' 
