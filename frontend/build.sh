rm -rf dist
npm run build
cd .. 
rm -rf data
cp -r frontend/dist data
rm build/assets.bin
cd frontend