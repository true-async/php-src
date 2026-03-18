#!/bin/bash
#
# Build PHP for Valgrind (without ASAN)
#
# This script rebuilds PHP with debug symbols and without address sanitizer,
# making it suitable for running under Valgrind.
#

set -e

echo "======================================"
echo "Building PHP for Valgrind"
echo "======================================"

# Clean previous build
echo "Cleaning previous build..."
make clean || true

# Rebuild configure
echo "Rebuilding configure..."
./buildconf --force

# Configure with same options but without ASAN
echo "Configuring..."
CFLAGS="-g -O1" \
LDFLAGS="" \
CXXFLAGS="-g -O1" \
./configure \
  --enable-async \
  --enable-pdo \
  --with-pdo-mysql \
  --with-sqlite3 \
  --with-curl \
  --enable-ftp \
  --with-pdo-pgsql \
  --with-mysqli \
  --enable-embed=shared \
  --with-openssl \
  --enable-sockets \
  --enable-mbstring \
  --enable-debug

# Build
echo "Building..."
make -j$(nproc)

echo ""
echo "======================================"
echo "Build completed!"
echo "======================================"
echo ""
echo "To run under Valgrind:"
echo ""
echo "  # CLI script:"
echo "  valgrind --leak-check=full --track-origins=yes \\"
echo "    --show-leak-kinds=all --verbose \\"
echo "    --log-file=valgrind.log \\"
echo "    ./sapi/cli/php your_script.php"
echo ""
echo "  # Or with suppression file:"
echo "  valgrind --leak-check=full --track-origins=yes \\"
echo "    --suppressions=./valgrind.supp \\"
echo "    --log-file=valgrind.log \\"
echo "    ./sapi/cli/php your_script.php"
echo ""
