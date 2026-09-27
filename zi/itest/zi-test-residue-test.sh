#!/bin/sh
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$here/zi-test-residue.sh"
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

ZI_LOGDIR=$root sh -c '
  . "$1"
  zi_residue_init shell-pass
  : >"$(zi_residue_path output)"
  zi_residue_finish 0
' sh "$here/zi-test-residue.sh"
test -z "$(find "$root" -mindepth 1 -maxdepth 1 -name 'shell-pass.*' -print)"

out=$(ZI_LOGDIR=$root sh -c '
  . "$1"
  zi_residue_init shell-fail
  trap '\''status=$?; zi_residue_finish "$status"'\'' EXIT
  : >"$(zi_residue_path output)"
  exit 1
' sh "$here/zi-test-residue.sh" 2>&1) && exit 1
case "$out" in
  *"# residue: $root/shell-fail."*) ;;
  *) echo "missing shell failure diagnostic: $out" >&2; exit 1 ;;
esac
test "$(find "$root" -mindepth 2 -maxdepth 2 -name output | wc -l)" -eq 1

out=$(ZI_LOGDIR=$root sh -c '
  . "$1"
  zi_residue_init shell-term
  trap '\''status=$?; zi_residue_finish "$status"'\'' EXIT
  trap '\''exit 143'\'' TERM
  : >"$(zi_residue_path output)"
  kill -TERM "$$"
' sh "$here/zi-test-residue.sh" 2>&1) && exit 1
case "$out" in
  *"# residue: $root/shell-term."*) ;;
  *) echo "missing shell TERM diagnostic: $out" >&2; exit 1 ;;
esac

for i in 1 2 3 4 5 6 7 8 9 10; do
  ZI_LOGDIR=$root sh -c '
    . "$1"
    zi_residue_init shell-age
    zi_residue_finish 1
  ' sh "$here/zi-test-residue.sh" >/dev/null
done
test "$(find "$root" -mindepth 1 -maxdepth 1 -name 'shell-age.*' | wc -l)" -eq 8

if test -d /dev/shm && test -w /dev/shm; then
  ZI_LOGDIR=$root sh -c '
    . "$1"
    zi_residue_init shell-shm
    name="shell-residue-$$"
    zi_residue_shm "$name"
    : >"/dev/shm/$name.ctrl"
    : >"/dev/shm/$name.data"
    zi_residue_finish 0
    test ! -e "/dev/shm/$name.ctrl" && test ! -e "/dev/shm/$name.data"
  ' sh "$here/zi-test-residue.sh"
fi

(
  cd "$root"
  ZI_LOGDIR=. sh -c '
    . "$1"
    zi_residue_init shell-relative
    : >"$(zi_residue_path output)"
    zi_residue_finish 0
  ' sh "$here/zi-test-residue.sh"
)
test -z "$(find "$root" -mindepth 1 -maxdepth 1 -name 'shell-relative.*' -print)"

if zi_residue_name '../escape'; then
  echo 'unsafe residue name accepted' >&2
  exit 1
fi
