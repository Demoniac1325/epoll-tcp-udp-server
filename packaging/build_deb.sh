#!/bin/sh
set -e

PREFIX=/usr/local
BINDIR="$PREFIX/bin"
UNITDIR="/lib/systemd/system"

PKGNAME="epoll-server"
PKGVER="1.0"
PKGDIR="build-deb"

rm -rf "$PKGDIR"
mkdir -p "$PKGDIR/DEBIAN"
chmod 755 "$PKGDIR/DEBIAN"
mkdir -p "$PKGDIR$BINDIR"
mkdir -p "$PKGDIR$UNITDIR"

cp server "$PKGDIR$BINDIR/server"
chmod 755 "$PKGDIR$BINDIR/server"

cp packaging/server.service "$PKGDIR$UNITDIR/server.service"
chmod 644 "$PKGDIR$UNITDIR/server.service"

cat > "$PKGDIR/DEBIAN/control" <<EOF
Package: $PKGNAME
Version: $PKGVER
Section: net
Priority: optional
Architecture: amd64
Maintainer: Your Name <you@example.com>
Description: Epoll-based TCP/UDP server with TCP and UDP support
EOF

dpkg-deb --build "$PKGDIR" "${PKGNAME}_${PKGVER}_amd64.deb"
