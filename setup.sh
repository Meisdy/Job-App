#!/bin/bash
set -e

sudo apt-get update -qq && sudo apt-get install -y unzip > /dev/null

if ! command -v docker &> /dev/null; then
  curl -fsSL https://get.docker.com | sudo sh
  sudo usermod -aG docker "$USER"
  exec sg docker "$0"
fi

curl -fsSL https://github.com/Meisdy/Job-App/archive/refs/heads/master.zip -o Job-App.zip
unzip -q Job-App.zip
mv Job-App-master Job-App
rm Job-App.zip
cd Job-App

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
