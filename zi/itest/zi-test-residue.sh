#!/bin/sh

# Source this file.  The caller owns EXIT/INT/TERM traps and stops children
# before calling zi_residue_finish with the saved exit status.

zi_residue_name()
{
  case "$1" in
    ''|.*|*..*|*/*|*\\*|*[!A-Za-z0-9._-]*) return 1 ;;
  esac
  return 0
}

zi_residue_init()
{
  zi_residue_name "$1" || return 2
  ZI_RESIDUE_NAME=$1
  ZI_RESIDUE_SHM_NAMES=
  zi_residue_root=${ZI_LOGDIR:-.}
  mkdir -p "$zi_residue_root" || return
  zi_residue_root=$(CDPATH= cd -P -- "$zi_residue_root" && pwd) || return
  ZI_RESIDUE_DIR=$(mktemp -d "$zi_residue_root/$ZI_RESIDUE_NAME.XXXXXXXX") || return
  zi_residue_start=
  if test -r "/proc/$$/stat"; then
    zi_residue_start=$(awk '{print $22}' "/proc/$$/stat") || return
  fi
  printf '%s %s\n' "$$" "$zi_residue_start" >"$ZI_RESIDUE_DIR/.active" || return
  export ZI_RESIDUE_DIR
  zi_residue_age
}

zi_residue_path()
{
  zi_residue_name "$1" || return 2
  test -n "${ZI_RESIDUE_DIR:-}" || return 2
  printf '%s/%s\n' "$ZI_RESIDUE_DIR" "$1"
}

zi_residue_shm()
{
  zi_residue_name "$1" || return 2
  case " $ZI_RESIDUE_SHM_NAMES " in
    *" $1 "*) return 2 ;;
  esac
  ZI_RESIDUE_SHM_NAMES="$ZI_RESIDUE_SHM_NAMES $1"
}

zi_residue_age()
{
  zi_residue_n=0
  if command -v flock >/dev/null 2>&1; then
    exec 9<"$zi_residue_root" || return
    flock -x 9 || return
  fi
  # Only exact named directories are considered; active processes are skipped.
  for zi_residue_candidate in "$zi_residue_root/$ZI_RESIDUE_NAME".*; do
    test -d "$zi_residue_candidate" || continue
    test -L "$zi_residue_candidate" && continue
    test "$zi_residue_candidate" = "$ZI_RESIDUE_DIR" && continue
    if test -f "$zi_residue_candidate/.active"; then
      read -r zi_residue_pid zi_residue_start <"$zi_residue_candidate/.active" || continue
      if kill -0 "$zi_residue_pid" 2>/dev/null; then
        if test -z "$zi_residue_start" || \
            test "$(awk '{print $22}' "/proc/$zi_residue_pid/stat" 2>/dev/null)" = "$zi_residue_start"; then
          continue
        fi
      fi
      mv "$zi_residue_candidate/.active" "$zi_residue_candidate/.failed" || return
    fi
  done
  # Sort only completed failures.  The top eight are retained.
  ls -dt "$zi_residue_root/$ZI_RESIDUE_NAME".* 2>/dev/null |
    while IFS= read -r zi_residue_candidate; do
      test -L "$zi_residue_candidate" && continue
      test -f "$zi_residue_candidate/.failed" || continue
      zi_residue_n=$((zi_residue_n + 1))
      if test "$zi_residue_n" -gt 8; then
        rm -rf -- "$zi_residue_candidate" || exit
      fi
    done
  zi_residue_result=$?
  if command -v flock >/dev/null 2>&1; then
    flock -u 9
    exec 9<&-
  fi
  return "$zi_residue_result"
}

zi_residue_finish()
{
  test -n "${ZI_RESIDUE_DIR:-}" || return 0
  zi_residue_done=$ZI_RESIDUE_DIR
  ZI_RESIDUE_DIR=
  export ZI_RESIDUE_DIR
  zi_residue_error=0
  for zi_residue_shm_name in $ZI_RESIDUE_SHM_NAMES; do
    if test -d /dev/shm; then
      rm -f -- "/dev/shm/$zi_residue_shm_name.ctrl" \
        "/dev/shm/$zi_residue_shm_name.data" || zi_residue_error=1
    fi
  done
  if test "$1" -eq 0; then
    rm -rf -- "$zi_residue_done" || zi_residue_error=1
  else
    mv "$zi_residue_done/.active" "$zi_residue_done/.failed" || return
    printf '# residue: %s\n' "$zi_residue_done"
    zi_residue_age || zi_residue_error=1
  fi
  return "$zi_residue_error"
}
