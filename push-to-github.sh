#!/bin/bash
# Vortex RPC — GitHub push script
# Run this from your macOS terminal (not in the VM)
#
# Prerequisites:
#   gh auth status  # verify you're logged in
#   gh repo create nana0739/vortex --public --source=. --push
#
# Or manually:
#   1. Create a repo at https://github.com/nana0739/vortex
#   2. Run the commands below

REPO_PATH="/Users/skat/Projects/vortex"

cd "$REPO_PATH" || exit 1

echo "=== Current state ==="
git log --oneline -1
echo ""

# Option A: Use gh CLI (recommended)
if command -v gh &> /dev/null && gh auth status &> /dev/null; then
    echo "Creating repo and pushing via gh CLI..."
    gh repo create nana0739/vortex --public --source=. --push --remote=origin
    echo "Done! Check https://github.com/nana0739/vortex"
    echo "CI should start automatically: https://github.com/nana0739/vortex/actions"
else
    # Option B: Manual — user needs to add remote and push
    echo "gh CLI not available. Please:"
    echo "1. Create repo at https://github.com/nana0739/vortex (public, empty)"
    echo "2. Run:"
    echo "   cd '$REPO_PATH'"
    echo "   git remote add origin git@github.com:nana0739/vortex.git"
    echo "   git push -u origin main"
fi
