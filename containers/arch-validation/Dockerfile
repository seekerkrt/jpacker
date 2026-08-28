FROM archlinux:latest

LABEL org.opencontainers.image.title="Moguet Arch validation" \
      org.opencontainers.image.source="https://github.com/seekerkrt/moguet" \
      org.opencontainers.image.base.name="docker.io/library/archlinux:latest"

# base-devel supplies the compiler, makepkg build tools, gettext, pkgconf,
# binutils, and GNU Make. The remaining packages cover Moguet's direct
# build/runtime contract and the shell, Python, locale, PTY, and completion
# tools exercised by the existing validation targets.
RUN pacman-key --init \
    && pacman-key --populate archlinux \
    && pacman -Syu --needed --noconfirm \
        base-devel \
        bash \
        clang \
        cmake \
        curl \
        fish \
        git \
        glibc \
        libarchive \
        nano \
        nlohmann-json \
        pacman \
        python \
        sudo \
        tomlplusplus \
        util-linux \
        zsh \
    && sed -i 's/^#\(en_US\.UTF-8 UTF-8\)/\1/' /etc/locale.gen \
    && locale-gen \
    && rm -f /var/cache/pacman/pkg/*

ARG VALIDATION_UID=1000
ARG VALIDATION_GID=1000

RUN groupadd --gid "${VALIDATION_GID}" moguet-validation \
    && useradd \
        --uid "${VALIDATION_UID}" \
        --gid "${VALIDATION_GID}" \
        --create-home \
        --shell /bin/bash \
        moguet-validation \
    && install -d -o moguet-validation -g moguet-validation -m 0700 \
        /home/moguet-validation/.cache \
        /home/moguet-validation/.config \
        /home/moguet-validation/.local/state \
    && install -d -o moguet-validation -g moguet-validation -m 0755 \
        /home/moguet-validation/fixtures/package-transition \
        /home/moguet-validation/work/moguet

ENV HOME=/home/moguet-validation \
    XDG_CACHE_HOME=/home/moguet-validation/.cache \
    XDG_CONFIG_HOME=/home/moguet-validation/.config \
    XDG_STATE_HOME=/home/moguet-validation/.local/state

WORKDIR /home/moguet-validation/work/moguet

# The named context contains only the host-generated legacy source archive.
# Git metadata and credentials remain outside every image layer.
COPY --from=moguet-transition-fixture \
    --chown=moguet-validation:moguet-validation \
    /jpacker-v1.16.0-source.tar \
    /home/moguet-validation/fixtures/package-transition/jpacker-v1.16.0-source.tar
COPY --chown=moguet-validation:moguet-validation . .

USER moguet-validation:moguet-validation

# Capture the copied current source before any build artifact is generated.
# The transition test consumes this archive without requiring a synthetic .git.
RUN bsdtar -cf \
    /home/moguet-validation/fixtures/package-transition/moguet-current-source.tar \
    .

# Keep this smoke check non-mutating with respect to the package database. It
# records the Arch identity and proves that the production/test tool boundary
# is available to the unprivileged validation user.
RUN set -eu; \
    printf '%s\n' ':: Arch validation environment'; \
    sed -n '1,8p' /etc/os-release; \
    command -v cmake ctest g++ make makepkg pacman pkg-config msgfmt python3 bash zsh fish \
        localedef script bsdtar readelf >/dev/null; \
    test "$(LC_ALL=en_US.UTF-8 locale charmap)" = UTF-8; \
    pkg-config --exists libalpm; \
    test -r /usr/include/alpm.h; \
    pacman --version | sed -n '1p'; \
    makepkg --version | sed -n '1p'; \
    g++ --version | sed -n '1p'

# The image build owns lane E's clean production build. Runtime validation
# consumes this artifact and adds the host-test and release-exclusive coverage
# without rebuilding production a second time.
RUN env -u MAKEFLAGS -u MFLAGS make clean
RUN env -u MAKEFLAGS -u MFLAGS make -j8 --output-sync=target

CMD ["containers/arch-validation/run-tests.sh"]
