#!/usr/bin/env bash
#
# create-version-tags.sh - Detect VirtualBox release commits and create git tags
#
# Scans Version.kmk commit history across all branches. Release commits
# follow a strict naming convention in their commit messages:
#
#   "7.1.16"              -> v7.1.16       (GA release)
#   "7.1.8 respin."       -> v7.1.8        (respin overrides original)
#   "7.2.0_BETA1."        -> v7.2.0-beta1  (pre-release)
#   "7.2.0_RC1."          -> v7.2.0-rc1    (release candidate)
#   "7.0.0 ALPHA1"        -> v7.0.0-alpha1 (alpha)
#   "6.1.0 GA"            -> v6.1.0        (GA suffix stripped)
#   "6.0.0_RC1 rebuild 2" -> v6.0.0-rc1    (rebuild overrides original)
#   "5.2.0 again."        -> v5.2.0        (retag overrides original)
#
# Commits are processed in chronological order so that respins, rebuilds,
# and retags naturally overwrite the original release commit -- the last
# commit for each version wins.
#
# Usage:
#   create-version-tags.sh [--dry-run] [--push] [--output FILE]
#
# Options:
#   --dry-run     Print what tags would be created without creating them
#   --push        Push new tags to origin after creating them
#   --output FILE Write newly-created tag names, one per line

#
# Copyright (C) 2006-2026 Oracle and/or its affiliates.
#
# This file is part of VirtualBox base platform packages, as
# available from https://www.virtualbox.org.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, in version 3 of the
# License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses>.
#
# SPDX-License-Identifier: GPL-3.0-only
#

set -euo pipefail
shopt -s extglob

DRY_RUN=false
PUSH=false
OUTPUT_FILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
    --dry-run)
        DRY_RUN=true
        shift
        ;;
    --push)
        PUSH=true
        shift
        ;;
    --output)
        if [[ $# -lt 2 || -z "$2" ]]; then
            echo "error: --output requires a file path" >&2
            exit 1
        fi
        OUTPUT_FILE="$2"
        shift 2
        ;;
    -h | --help)
        sed -n '2,/^[^#]/{ /^#/s/^# \?//p }' "$0"
        exit 0
        ;;
    *)
        echo "error: unknown option: $1" >&2
        echo "usage: $0 [--dry-run] [--push] [--output FILE]" >&2
        exit 1
        ;;
    esac
done

if [[ -n "$OUTPUT_FILE" ]]; then
    : >"$OUTPUT_FILE"
fi

# ---------------------------------------------------------------------------
# Collect existing tags for fast lookup
# ---------------------------------------------------------------------------
declare -A existing_tags
while IFS= read -r tag; do
    [[ -n "$tag" ]] && existing_tags["$tag"]=1
done < <(git tag -l 'v*')

# ---------------------------------------------------------------------------
# Walk all Version.kmk commits (chronological order) and detect releases
# ---------------------------------------------------------------------------
declare -A tag_map      # tag name -> commit hash
declare -a tag_order=() # preserve discovery order for deterministic output

while IFS=' ' read -r hash msg; do
    # Strip trailing whitespace, dots, and periods for matching
    msg_clean="${msg%"${msg##*[^. ]}"}"

    # Match release patterns:
    #   X.Y.Z                          (GA release)
    #   X.Y.Z_BETAX / X.Y.Z_RCX       (pre-release with underscore)
    #   X.Y.Z ALPHAX / X.Y.Z AlphaX   (pre-release with space)
    #   X.Y.Z GA                       (explicit GA marker)
    # Optionally followed by: respin, rebuild [N], again
    if [[ "$msg_clean" =~ ^([0-9]+\.[0-9]+\.[0-9]+)([_ ](BETA[0-9]*|RC[0-9]*|ALPHA[0-9]*|Alpha[0-9]*|GA))?(\ (respin|rebuild[^.]*|again))?$ ]]; then
        version="${BASH_REMATCH[1]}"
        prerelease="${BASH_REMATCH[3]}"

        # Build tag name
        tag="v${version}"
        if [[ -n "$prerelease" ]]; then
            prerelease_lower="$(echo "$prerelease" | tr '[:upper:]' '[:lower:]')"
            # "GA" is not a pre-release suffix -- it just confirms the release
            if [[ "$prerelease_lower" != "ga" ]]; then
                tag="${tag}-${prerelease_lower}"
            fi
        fi

        # Record (overwrites previous entry for same tag -- last one wins)
        if [[ -z "${tag_map[$tag]:-}" ]]; then
            tag_order+=("$tag")
        fi
        tag_map["$tag"]="$hash"
    fi
done < <(git log --all --reverse --format='%H %s' -- Version.kmk)

# ---------------------------------------------------------------------------
# Create missing tags
# ---------------------------------------------------------------------------
created=0
skipped=0
declare -a created_tags=()

for tag in "${tag_order[@]}"; do
    hash="${tag_map[$tag]}"
    if [[ -n "${existing_tags[$tag]:-}" ]]; then
        skipped=$((skipped + 1))
        continue
    fi

    if [[ "$DRY_RUN" == true ]]; then
        echo "[dry-run] would create tag $tag -> ${hash:0:12}"
    else
        git tag "$tag" "$hash"
        echo "created tag $tag -> ${hash:0:12}"
    fi
    created_tags+=("$tag")
    created=$((created + 1))
done

if [[ -n "$OUTPUT_FILE" ]]; then
    printf '%s\n' "${created_tags[@]}" >"$OUTPUT_FILE"
fi

echo ""
echo "summary: $created new, $skipped already existed, ${#tag_map[@]} total detected"

# ---------------------------------------------------------------------------
# Push if requested
# ---------------------------------------------------------------------------
if [[ "$PUSH" == true && "$DRY_RUN" == false && "$created" -gt 0 ]]; then
    for tag in "${created_tags[@]}"; do
        git push origin "refs/tags/$tag"
    done
    echo "pushed $created tag(s) to origin"
elif [[ "$PUSH" == true && "$created" -eq 0 ]]; then
    echo "no new tags to push"
fi
