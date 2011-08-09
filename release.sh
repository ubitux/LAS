#!/bin/sh

[ $# -eq 0 ] && echo "Usage: $0 release_version" && exit 0
git archive -v --format=tar --prefix=las-$1/ HEAD | xz -9 > las-$1.tar.xz
