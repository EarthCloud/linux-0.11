#!/usr/bin/env bash
# Linux 0.11（GCC 方言 + 中文注释版）—— 编译 / 运行 / 调试
# 用法: ./lab.sh [build|run|debug|shot|clean|cscope|help]
set -uo pipefail

REF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QEMU=qemu-system-i386
HDA="$REF/hdc-0.11.img"
IMG="$REF/Image"

usage() {
  cat <<'EOF'
用法: ./lab.sh <命令>

  build   编译内核，产出 Image / tools/system / System.map
  run     在 QEMU 里启动（需要图形界面 / WSLg）
  debug   以 -s -S 启动 QEMU，等 gdb 从本机 1234 端口连接
  shot    无图形环境下启动并截屏到 docs/boot.png
  cscope  重新生成 cscope 索引
  clean   清理编译产物
  help    显示本帮助

首次使用:  ./lab.sh build   然后   ./lab.sh run

调试（两个终端）:
  终端1:  ./lab.sh debug
  终端2:  gdb tools/system -ex 'target remote :1234' -ex 'b main' -ex 'c'
EOF
}

need_image() {
  [ -f "$IMG" ] || { echo "找不到 $IMG，请先执行: ./lab.sh build" >&2; exit 1; }
}

case "${1:-help}" in
  build)
    cd "$REF" || exit 1
    make clean >/dev/null 2>&1
    make && { echo; echo "==> 编译完成"; ls -l Image tools/system System.map; }
    ;;
  run)
    need_image
    exec $QEMU -m 16M -boot a -fda "$IMG" -hda "$HDA" -snapshot
    ;;
  debug)
    need_image
    echo "QEMU 已暂停，等待 gdb 连接 localhost:1234 ..."
    exec $QEMU -m 16M -boot a -fda "$IMG" -hda "$HDA" -snapshot -s -S
    ;;
  shot)
    need_image
    mkdir -p "$REF/docs"
    rm -f "$REF/docs/boot.png"
    # QEMU 10+ 的 screendump 支持 -f png，直接输出 PNG，无需外部转换工具
    ( sleep 22; echo "screendump $REF/docs/boot.png -f png"; sleep 2; echo "quit" ) | \
      timeout 90 $QEMU -m 16M -boot a -fda "$IMG" -hda "$HDA" -snapshot \
        -vga std -display none -monitor stdio >/dev/null 2>&1
    ls -l "$REF/docs/boot.png"
    ;;
  cscope)
    cd "$REF" || exit 1
    find boot fs include init kernel lib mm tools -type f \
      \( -name '*.c' -o -name '*.h' -o -name '*.s' -o -name '*.S' -o -name 'Makefile*' \) \
      | sort > cscope.files
    cscope -b -q -k && echo "cscope 索引已生成（cscope.out）"
    ;;
  clean)
    cd "$REF" && make clean && rm -f boot.log && echo "已清理"
    ;;
  help|-h|--help)
    usage
    ;;
  *)
    echo "未知命令: $1" >&2; usage; exit 1
    ;;
esac