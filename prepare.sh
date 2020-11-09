#!/bin/sh

git config core.hooksPath maint/hooks 2> /dev/null || true

echo "Regenerating build files..."
autoreconf -fi -Im4
