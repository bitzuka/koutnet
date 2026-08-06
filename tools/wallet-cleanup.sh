#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#
# Removes the entries a pre-wip23 test run left in the KOutNet folder of the
# real KWallet. Those runs reached KWallet because there is one wallet per
# session and QStandardPaths test mode does not move it; SecretStore now keeps
# test secrets in memory, so nothing new arrives here. This is for what is
# already there. kwallet-query can read an entry but not delete one, so this
# goes through the same D-Bus interface KWallet::Wallet itself uses.
#
#   tools/wallet-cleanup.sh                    list, change nothing
#   tools/wallet-cleanup.sh --remove           delete the test-scope entries
#   tools/wallet-cleanup.sh --remove --backups also delete the *_superseded ones
#
# Read the listing before passing --remove. The suites also constructed
# CryptoManager with no storage scope, which uses the same entry names the
# application does, so identity_priv_b64 and dh_priv_b64 may hold a key a test
# generated rather than the real one. Those two are never touched here: deleting
# them makes the next start mint a fresh identity and changes the fingerprint
# every peer has pinned. If the listing shows them and the fingerprint in the
# About page is not the one your peers know, that is what happened, and minting
# a new one deliberately is the fix.
set -u

SERVICE=org.kde.kwalletd6
OBJECT=/modules/kwalletd6
IFACE=org.kde.KWallet
APPID=koutnet-wallet-cleanup
FOLDER=KOutNet

remove=0
backups=0
for arg in "$@"; do
    case "$arg" in
        --remove) remove=1 ;;
        --backups) backups=1 ;;
        -h|--help) sed -n '5,26p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

QDBUS=
for candidate in qdbus6 qdbus-qt6 qdbus; do
    if command -v "$candidate" >/dev/null 2>&1; then
        QDBUS=$candidate
        break
    fi
done
if [ -z "$QDBUS" ]; then
    echo "no qdbus binary found (try installing qt6-tools / qttools-dev-tools)" >&2
    exit 1
fi

call() {
    "$QDBUS" "$SERVICE" "$OBJECT" "$IFACE.$1" "${@:2}"
}

wallet=$(call localWallet) || exit 1
if [ -z "$wallet" ]; then
    echo "kwalletd6 did not name a local wallet - is it running?" >&2
    exit 1
fi

handle=$(call open "$wallet" 0 "$APPID") || exit 1
if [ -z "$handle" ] || [ "$handle" -lt 0 ] 2>/dev/null; then
    echo "could not open the wallet '$wallet' (it may need unlocking)" >&2
    exit 1
fi
trap 'call close "$handle" false "$APPID" >/dev/null 2>&1 || true' EXIT

# Every storage scope the suites pass to CryptoManager. Kept as a literal list
# rather than a wildcard on purpose: a pattern wide enough to catch these is
# also wide enough to catch a scope a future build gives a real account.
scopes='peer-a peer-b peer-c peer-donor peer-evil peer-newcomer nm-impostor nm-impostor-id'

is_test_entry() {
    local key=$1 scope
    for scope in $scopes; do
        case "$key" in
            "identity_priv_b64_$scope"|"dh_priv_b64_$scope") return 0 ;;
            "identity_priv_b64_${scope}_superseded"|"dh_priv_b64_${scope}_superseded") return 0 ;;
        esac
    done
    return 1
}

is_backup_entry() {
    case "$1" in
        identity_priv_b64_superseded|dh_priv_b64_superseded) return 0 ;;
    esac
    return 1
}

entries=$(call entryList "$handle" "$FOLDER" "$APPID")
if [ -z "$entries" ]; then
    echo "the $FOLDER folder in '$wallet' is empty; nothing to do"
    exit 0
fi

doomed=()
kept=()
while IFS= read -r key; do
    [ -n "$key" ] || continue
    if is_test_entry "$key" || { [ "$backups" -eq 1 ] && is_backup_entry "$key"; }; then
        doomed+=("$key")
    else
        kept+=("$key")
    fi
done <<< "$entries"

echo "wallet: $wallet   folder: $FOLDER"
echo
echo "left alone (${#kept[@]}):"
for key in "${kept[@]:-}"; do
    [ -n "$key" ] || continue
    if is_backup_entry "$key"; then
        echo "  $key   <- migration backup; pass --backups to remove it too"
    elif [ "$key" = identity_priv_b64 ] || [ "$key" = dh_priv_b64 ]; then
        echo "  $key   <- your account's own key; read the note at the top of this script"
    else
        echo "  $key"
    fi
done
echo
echo "test leftovers (${#doomed[@]}):"
for key in "${doomed[@]:-}"; do
    [ -n "$key" ] && echo "  $key"
done

if [ ${#doomed[@]} -eq 0 ]; then
    echo
    echo "nothing to remove"
    exit 0
fi
if [ "$remove" -eq 0 ]; then
    echo
    echo "nothing was changed; pass --remove to delete the entries listed above"
    exit 0
fi

echo
failed=0
for key in "${doomed[@]}"; do
    if [ "$(call removeEntry "$handle" "$FOLDER" "$key" "$APPID")" = "0" ]; then
        echo "removed $key"
    else
        echo "FAILED to remove $key" >&2
        failed=1
    fi
done
exit "$failed"
