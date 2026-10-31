#!/bin/zsh
set -e
cd "/Users/akash/Desktop/Software Project/ALI"
git add -A
git commit -F .git/COMMIT_MSG
git push origin main
rm -- "$0"
