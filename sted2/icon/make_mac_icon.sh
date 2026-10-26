!#/bin/bash

# 1. iconset用フォルダを作成
mkdir sted.iconset

# 2. sipsコマンドを使って、元画像（sted-icon-smooth-1152x1152.png）から各サイズの画像を自動生成する
sips -z 16 16     sted-icon-smooth-1152x1152.png --out sted.iconset/icon_16x16.png
sips -z 32 32     sted-icon-smooth-1152x1152.png --out sted.iconset/icon_16x16@2x.png
sips -z 32 32     sted-icon-smooth-1152x1152.png --out sted.iconset/icon_32x32.png
sips -z 64 64     sted-icon-smooth-1152x1152.png --out sted.iconset/icon_32x32@2x.png
sips -z 128 128   sted-icon-smooth-1152x1152.png --out sted.iconset/icon_128x128.png
sips -z 256 256   sted-icon-smooth-1152x1152.png --out sted.iconset/icon_128x128@2x.png
sips -z 256 256   sted-icon-smooth-1152x1152.png --out sted.iconset/icon_256x256.png
sips -z 512 512   sted-icon-smooth-1152x1152.png --out sted.iconset/icon_256x256@2x.png
sips -z 512 512   sted-icon-smooth-1152x1152.png --out sted.iconset/icon_512x512.png
sips -z 1024 1024 sted-icon-smooth-1152x1152.png --out sted.iconset/icon_512x512@2x.png

# 3. iconutilコマンドで icns ファイルに変換する
iconutil -c icns sted.iconset

