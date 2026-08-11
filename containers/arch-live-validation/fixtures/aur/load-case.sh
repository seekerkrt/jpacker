#!/bin/sh

# Load the one reviewed live AUR scenario without copying its identity or
# pinned hashes into each transport consumer.  Callers remain responsible for
# enforcing the behavior they support (for example, x86_64 execution).
validation_load_aur_case() {
    validation_aur_case_file=$1
    validation_aur_tab=$(printf '\tX')
    validation_aur_tab=${validation_aur_tab%X}
    validation_aur_expected_header=$(printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s' \
        '# package' package_base expected_version runtime_dependencies \
        make_dependencies source_kind install_reason fallback_policy \
        review_required expected_aur_git_head expected_pkgbuild_sha256 \
        expected_srcinfo_sha256 expected_source_filename expected_source_url \
        expected_source_sha256 expected_rpc_url_path \
        expected_artifact_architecture)

    validation_aur_header=
    AUR_CASE_PACKAGE_NAME=
    AUR_CASE_PACKAGE_BASE=
    AUR_CASE_EXPECTED_VERSION=
    AUR_CASE_RUNTIME_DEPENDENCIES=
    AUR_CASE_MAKE_DEPENDENCIES=
    AUR_CASE_SOURCE_KIND=
    AUR_CASE_INSTALL_REASON=
    AUR_CASE_FALLBACK_POLICY=
    AUR_CASE_REVIEW_REQUIRED=
    AUR_CASE_EXPECTED_GIT_HEAD=
    AUR_CASE_EXPECTED_PKGBUILD_SHA256=
    AUR_CASE_EXPECTED_SRCINFO_SHA256=
    AUR_CASE_EXPECTED_SOURCE_FILENAME=
    AUR_CASE_EXPECTED_SOURCE_URL=
    AUR_CASE_EXPECTED_SOURCE_SHA256=
    AUR_CASE_EXPECTED_RPC_URL_PATH=
    AUR_CASE_EXPECTED_ARCHITECTURE=
    validation_aur_extra_field=
    {
        IFS= read -r validation_aur_header || return 1
        IFS=$validation_aur_tab read -r \
            AUR_CASE_PACKAGE_NAME AUR_CASE_PACKAGE_BASE \
            AUR_CASE_EXPECTED_VERSION AUR_CASE_RUNTIME_DEPENDENCIES \
            AUR_CASE_MAKE_DEPENDENCIES AUR_CASE_SOURCE_KIND \
            AUR_CASE_INSTALL_REASON AUR_CASE_FALLBACK_POLICY \
            AUR_CASE_REVIEW_REQUIRED AUR_CASE_EXPECTED_GIT_HEAD \
            AUR_CASE_EXPECTED_PKGBUILD_SHA256 \
            AUR_CASE_EXPECTED_SRCINFO_SHA256 \
            AUR_CASE_EXPECTED_SOURCE_FILENAME \
            AUR_CASE_EXPECTED_SOURCE_URL AUR_CASE_EXPECTED_SOURCE_SHA256 \
            AUR_CASE_EXPECTED_RPC_URL_PATH AUR_CASE_EXPECTED_ARCHITECTURE \
            validation_aur_extra_field || return 1
        if IFS= read -r validation_aur_unused_row; then
            return 1
        fi
    } < "$validation_aur_case_file"

    [ "$validation_aur_header" = "$validation_aur_expected_header" ] || return 1
    [ -z "$validation_aur_extra_field" ] || return 1
    [ "$AUR_CASE_PACKAGE_NAME" = "$AUR_CASE_PACKAGE_BASE" ] || return 1
    case "$AUR_CASE_PACKAGE_NAME" in
        ''|*[!a-z0-9@._+-]*) return 1 ;;
    esac
    case "$AUR_CASE_EXPECTED_VERSION" in
        ''|*[!A-Za-z0-9@._+:~=-]*) return 1 ;;
    esac
    case "$AUR_CASE_RUNTIME_DEPENDENCIES,$AUR_CASE_MAKE_DEPENDENCIES" in
        *[!A-Za-z0-9@._+,:\<\>==-]*) return 1 ;;
    esac
    [ "$AUR_CASE_SOURCE_KIND" = single-release-archive ] || return 1
    [ "$AUR_CASE_INSTALL_REASON" = Explicit ] || return 1
    [ "$AUR_CASE_FALLBACK_POLICY" = reject ] || return 1
    [ "$AUR_CASE_REVIEW_REQUIRED" = required ] || return 1
    case "$AUR_CASE_EXPECTED_GIT_HEAD" in
        *[!0-9a-f]*|'') return 1 ;;
    esac
    [ "${#AUR_CASE_EXPECTED_GIT_HEAD}" -eq 40 ] || return 1
    for validation_aur_sha256 in \
        "$AUR_CASE_EXPECTED_PKGBUILD_SHA256" \
        "$AUR_CASE_EXPECTED_SRCINFO_SHA256" \
        "$AUR_CASE_EXPECTED_SOURCE_SHA256"
    do
        case "$validation_aur_sha256" in
            *[!0-9a-f]*|'') return 1 ;;
        esac
        [ "${#validation_aur_sha256}" -eq 64 ] || return 1
    done
    case "$AUR_CASE_EXPECTED_SOURCE_FILENAME" in
        ''|*/*) return 1 ;;
    esac
    case "$AUR_CASE_EXPECTED_SOURCE_URL" in
        https://*) ;;
        *) return 1 ;;
    esac
    case "$AUR_CASE_EXPECTED_RPC_URL_PATH" in
        /*) ;;
        *) return 1 ;;
    esac
    case "$AUR_CASE_EXPECTED_RPC_URL_PATH" in
        *../*|*/..) return 1 ;;
    esac
    case "$AUR_CASE_EXPECTED_ARCHITECTURE" in
        ''|*[!A-Za-z0-9_+-]*) return 1 ;;
    esac

    AUR_CASE_EXPECTED_ARTIFACT_FILENAME="${AUR_CASE_PACKAGE_NAME}-${AUR_CASE_EXPECTED_VERSION}-${AUR_CASE_EXPECTED_ARCHITECTURE}.pkg.tar.zst"
    AUR_CASE_DEBUG_PACKAGE_NAME="${AUR_CASE_PACKAGE_NAME}-debug"
    AUR_CASE_README_PATH="usr/share/doc/${AUR_CASE_PACKAGE_NAME}/README.md"
}
