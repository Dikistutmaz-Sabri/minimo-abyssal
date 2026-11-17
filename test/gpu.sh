#!/bin/sh
# GPU time (amdgpu "drm-engine-gfx", from /proc/<pid>/fdinfo) used by the processes whose command
# line contains $1, over $2 seconds, as a percent of one GPU.
pat="$1"; secs="${2:-5}"
sum() { for p in $(pgrep -f -- "$pat"); do cat /proc/$p/fdinfo/* 2>/dev/null; done | awk '/^drm-client-id/{id=$2} /^drm-engine-gfx:/{v[id]=$2} END{s=0; for(i in v) s+=v[i]; print s}'; }
a=$(sum); sleep "$secs"; b=$(sum)
awk -v a="$a" -v b="$b" -v s="$secs" 'BEGIN{printf "%.1f%%\n", (b-a)/(s*1e9)*100}'
