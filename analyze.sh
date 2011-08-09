#!/bin/sh
# vim: set et sts=4 sw=4:
#
# LAS, Lossy Audio Spotter (shell script helper)
# Copyright (C) 2011  Clément Bœsch
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

scan_file() {
    out=`./las "$1"`
    img="data:image/png;base64,`echo "$out" | gnuplot | base64`"
    info=`echo "$out" | grep '^#'`
    class=`echo "$info" | grep lossy >/dev/null && echo lossy || echo lossless`
    echo "<div class=\"$class\"><h1>$1</h1><img src=\"$img\" alt=\"$1\" /><p>$info</p></div>"
}

[ $# -eq 0 ] && echo "Usage: $0 FILE|DIR ..." && exit

for dep in gnuplot base64; do
    ! which $dep >&/dev/null && echo "Unable to find $dep" && exit 1
done

OUTPUT=report.html
STYLE="
    body { background-color:#000; color:#fff; font-size:11px; }
    div.lossy,div.lossless { padding:5px; margin:2px; float:left; width:300px; height:280px; }
    h1 { font-size: 12px; }
    div.lossy    { background-color: #db2f2f; }
    div.lossless { background-color: #1e8c7a; }
"
echo "<!doctype html><html><head><title>LAS Analysis</title><style>$STYLE</style></head><body>" > $OUTPUT
for f in "$@"; do
    [ ! -f "$f" -a ! -d "$f" ] && continue
    IFS_BAK="$IFS"; IFS=$'\n'
    for sf in $(find "$f" -type f -name '*.flac' | sort); do
        echo "Analyzing $sf..."
        scan_file "$sf" >> $OUTPUT
    done
    IFS="$IFS_BAK"
done
echo '</body></html>' >> $OUTPUT
