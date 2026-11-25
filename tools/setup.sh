#!/bin/bash
# Builds the tools' environment, plumb, from environment.yml with micromamba or conda, replacing
# any earlier one, then installs plumb_tools into it editable. Safe to rerun.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)

if command -v micromamba >/dev/null; then
    tool=micromamba
elif [[ -x ~/.local/bin/micromamba ]]; then
    tool=~/.local/bin/micromamba
elif command -v conda >/dev/null; then
    tool=conda
else
    echo "setup: needs micromamba or conda" >&2
    exit 1
fi

if [[ $tool == conda ]]; then
    conda env create -y -n plumb -f "$here/environment.yml"
else
    "$tool" create -y -n plumb -f "$here/environment.yml"
fi
"$tool" run -n plumb python -m pip install --no-deps --no-build-isolation -e "$here"
"$tool" run -n plumb python -c "import plumb_tools, numpy, matplotlib, sys; print('plumb:', sys.version.split()[0], numpy.__version__, matplotlib.__version__)"
