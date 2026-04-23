#!/bin/bash
set -e

git clone https://github.com/Meisdy/Job-App
cd job-app

cat > config/api_keys.json << 'EOF'
{
  "api_key": "YOUR_API_KEY_HERE"
}
EOF

mkdir -p data

echo "Building and starting (takes ~2 min first time)..."
docker compose up --build -d

echo ""
echo "Done. Edit config/api_keys.json with your API key, then run:"
echo "  docker compose restart"
echo ""
echo "Open http://localhost:8080"
