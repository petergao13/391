
#!/bin/bash
# Open an interactive bash shell inside the build container.
# The project root is mounted at /work.
set -e
cd "$(dirname "$0")"

echo "==> Building Docker image (cached after first run)..."
docker build -t wonton-os . 2>&1 | tail -1

echo ""
echo "==> Starting shell. Project is at /work."
echo "    Build:  make -C usr all && make -C sys all"
echo "    Run:    make -C sys run"
echo ""

docker run --rm -it \
    --platform linux/amd64 \
    -v "$(pwd):/work" \
    wonton-os bash
