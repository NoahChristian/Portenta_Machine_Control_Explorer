#!/bin/sh
# Copy tools/pre-commit into .git/hooks/, where git will actually run it.
#
# A hook lives in .git/hooks and is NOT committed, so it is per-clone and
# silently absent in a fresh clone. tools/pre-commit is the source; this copies
# it. Run it after editing the source, then PLANT A VIOLATION and watch the
# commit be refused - a control that has never been seen to fire has not been
# tested.
#
# The global commit-msg gate is installed separately, by
# ~/.claude/scripts/security/installhooks.py --apply, because it is a property
# of this machine rather than of this repo.
set -e
root=$(git rev-parse --show-toplevel)
cp "$root/tools/pre-commit" "$root/.git/hooks/pre-commit"
chmod +x "$root/.git/hooks/pre-commit"
echo "installed: .git/hooks/pre-commit"
if ! cmp -s "$root/tools/pre-commit" "$root/.git/hooks/pre-commit"; then
  echo "installhook: copy does not match source - refusing to claim success" >&2
  exit 1
fi
echo "verified: the installed copy is byte-identical to tools/pre-commit"
