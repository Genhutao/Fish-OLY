#!/bin/bash
cd "$(dirname "$0")" || exit 1
export PATH="$HOME/.local/go/bin:$PATH"

if [ ! -f ../client/dist/index.html ]; then
  echo "首次运行，构建客户端…"
  ( cd ../client && pnpm install && pnpm build ) || exit 1
fi

echo "编译服务端…"
go build -o bin/oly-engine . || exit 1

( sleep 1; open "http://127.0.0.1:8787" ) &
# -tick-step 2s：1 tick = 2 秒真实时间（正式口径 1 tick = 1 分钟，演示压缩便于看日程推进）
exec ./bin/oly-engine -data ./data -tick-step 2s -web ../client/dist
