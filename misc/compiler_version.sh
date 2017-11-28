#!/bin/bash
set -eo pipefail
echo '
#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER)
gcc#__GNUC__#__GNUC_MINOR__#__GNUC_PATCHLEVEL__
#elif defined(__INTEL_COMPILER)
icc#__INTEL_COMPILER#__INTEL_COMPILER#__INTEL_COMPILER_UPDATE
#elif defined(__clang__)
clang#__clang_major__#__clang_minor__#__clang_patchlevel__
#else
unknown
#endif' |  $1 -E - | sed --silent '/\(^[^#]..*$\)/s/#/_/gp' | awk -F_ '$1 != "icc"{print} $1 == "icc" {print "icc_"$2/100"_"$3%100"_"$4}'
