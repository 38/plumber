#!/bin/bash
set -eo pipefail
echo '
#if defined(__GNUC__) && !defined(__clang__)
gcc#__GNUC__#__GNUC_MINOR__#__GNUC_PATCHLEVEL__
#elif defined(__clang__)
clang#__clang_major__#__clang_minor__#__clang_patchlevel__
#else
unknown
#endif' |  $1 -E - | sed --silent '/\(^[^#]..*$\)/s/#/_/gp'
