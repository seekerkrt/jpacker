FROM moguet-arch-validation:local

LABEL org.opencontainers.image.title="Moguet trusted ALPM receipt validation" \
      org.opencontainers.image.source="https://github.com/seekerkrt/moguet"

USER root:root

RUN install -d -o moguet-validation -g moguet-validation -m 0755 \
        /home/moguet-validation/work/moguet-receipt \
        /home/moguet-validation/receipt-packages

WORKDIR /home/moguet-validation/work/moguet-receipt

# The standalone Docker build context is filtered by .dockerignore. No host
# worktree mount, Git metadata, credential, XDG data, or package database is
# available to the runtime validation container.
COPY --chown=moguet-validation:moguet-validation . .

USER moguet-validation:moguet-validation

RUN env -u MAKEFLAGS -u MFLAGS make clean \
    && env -u MAKEFLAGS -u MFLAGS make \
        PREFIX=/usr -j8 --output-sync=target

RUN for fixture in \
        dependency-v1 \
        dependency-v2 \
        target; do \
        ( \
            cd "containers/arch-receipt-validation/fixtures/$fixture" \
            && PKGDEST=/home/moguet-validation/receipt-packages \
                makepkg --nodeps --noconfirm \
        ) || exit $?; \
    done

USER root:root

# Install through the configured CMake graph. The hook will execute only this
# root-owned installed payload; the build-tree helper remains unexecuted.
RUN cmake --install build/cmake-production \
    && test "$(stat -c '%U:%G:%a:%F' \
        /usr/libexec/moguet/moguet-alpm-receipt-helper)" = \
        'root:root:755:regular file' \
    && test "$(stat -c '%U:%G:%a:%F' \
        /usr/libexec/moguet/moguet-makepkg-syncdeps-adapter)" = \
        'root:root:755:regular file' \
    && printf '%s\n' \
        'moguet-validation ALL=(root) NOPASSWD: /usr/libexec/moguet/moguet-makepkg-syncdeps-adapter *' \
        > /etc/sudoers.d/moguet-makepkg-syncdeps-validation \
    && chown root:root /etc/sudoers.d/moguet-makepkg-syncdeps-validation \
    && chmod 0440 /etc/sudoers.d/moguet-makepkg-syncdeps-validation \
    && visudo -cf /etc/sudoers.d/moguet-makepkg-syncdeps-validation

CMD ["/bin/sh", "containers/arch-receipt-validation/run-installed-receipt.sh"]
