#!/bin/bash
# 将 output.m3u8 中的绝对分片 URL 改回相对路径，解决虚拟机 IP 变更后 HLS 无法播放。
# 用法：sudo bash fix_hls_m3u8_relative.sh [/path/to/hls]
set -euo pipefail

HLS_DIR="${1:-/home/user/project/code/server_qt/hls}"

if [[ ! -d "$HLS_DIR" ]]; then
  echo "目录不存在: $HLS_DIR" >&2
  exit 1
fi

count=0
while IFS= read -r -d '' f; do
  sed -i -E 's|^https?://[^/]+/hls/[^/]+/(segment_[0-9]+\.ts)$|\1|Ig' "$f"
  echo "已修复: $f"
  count=$((count + 1))
done < <(find "$HLS_DIR" -name 'output.m3u8' -print0)

echo "完成，共处理 ${count} 个 m3u8 文件。"
