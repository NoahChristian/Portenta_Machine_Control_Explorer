# Flag a size or reg assigned DIRECTLY on the flash0 node.
#
# THE trap on this SoC. stm32h747Xi_m7.dtsi declares DT_SIZE_K(1024), which
# reads like "bank 1 only", and drivers/flash/flash_stm32h7x.c DOUBLES it on
# dual-core parts:
#
#     #if defined(CONFIG_STM32H7_DUAL_CORE)
#     #define REAL_FLASH_SIZE_KB (DT_REG_SIZE(...) * 2)
#
# So declaring the real 2 MB makes the driver believe 4 MB, which moves the
# bank boundary inside get_sector() from 1 MB to 2 MB and classifies every
# bank-2 address as bank 1. PG is then set in CR1 for a bank-2 address, bank 2
# raises PGSERR, and the write fails with -EIO while erase of the SAME sector
# still returns 0. MCUboot halts in arch_system_halt with NO fault bits set
# (CFSR=0, HFSR=0) and no log line, which looks nothing like a flash problem.
# On the Giga it cost four blind debugging attempts.
#
# TWO THINGS THIS CHECK GOT WRONG BEFORE IT WORKED, both found by planting the
# violation rather than by reading the code:
#
#  1. The first version grepped for flash0 and for "reg =" on the SAME line,
#     so the ordinary two-line form sailed straight through:
#         &flash0 {
#                 reg = <0x08000000 DT_SIZE_M(2)>;
#
#  2. The second version tracked brace depth per line, which fixed that but
#     still missed the one-line form, where the property shares the line with
#     the brace that opens the node:
#         &flash0 { size = <DT_SIZE_M(2)>; };
#
# Hence the normalization below: braces and semicolons each get their own line
# before any depth is counted, so where a property sits on the page cannot
# change the answer.
#
# WHY DEPTH MATTERS AT ALL. A partition's own reg is legitimate, and
# pmc-partitions.dtsi is full of them:
#
#     &flash0 { partitions { boot_partition: partition@0 { reg = <...>; }; }; };
#       depth 1            depth 2         depth 3        <- allowed here
#
# Only depth 1 - the flash0 node itself - is the error. A check that also fired
# at depth 3 would fire on every commit that touches the flash map, and a gate
# that fires on routine work is one people learn to --no-verify past.
#
# Reports the ORIGINAL line number, not the normalized one, so the message
# points at something you can open.

{
    line = $0
    sub(/\/\/.*$/, "", line)          # a trailing line comment is not devicetree
    gsub(/\/\*[^\/]*\*\//, "", line)  # nor a self-contained block one

    # A MULTI-LINE comment has to be tracked, not pattern-matched. The whole
    # point of pmc-partitions.dtsi is a long comment explaining why size is not
    # set here, and that comment contains the words "size = <DT_SIZE_M(2)>".
    # Relying on the leading "*" of the usual comment style would make this
    # check fire on its own documentation the first time somebody wrapped a
    # line differently - a false positive on routine work, which is how a gate
    # ends up bypassed.
    if (incomment) {
        if (sub(/^.*\*\//, "", line)) { incomment = 0 } else { line = "" }
    }
    if (sub(/\/\*.*$/, "", line)) { incomment = 1 }

    raw[NR] = $0
    src[NR] = line
    n = NR
}

END {
    inflash = 0
    depth   = 0
    bad     = 0

    for (i = 1; i <= n; i++) {
        line = src[i]

        # Normalize: one token per element, so brace placement is irrelevant.
        gsub(/\{/, "\n{\n", line)
        gsub(/\}/, "\n}\n", line)
        gsub(/;/,  ";\n",   line)
        cnt = split(line, tok, "\n")

        for (t = 1; t <= cnt; t++) {
            piece = tok[t]
            if (piece ~ /^[[:space:]]*$/) continue

            if (piece == "{") {
                if (pending_flash && depth == 0) { inflash = 1 }
                pending_flash = 0
                depth++
                continue
            }
            if (piece == "}") {
                depth--
                if (inflash && depth <= 0) { inflash = 0; depth = 0 }
                continue
            }

            # A reference or a nodelabel naming flash0, awaiting its brace.
            if (!inflash && piece ~ /(^|[^A-Za-z0-9_])flash0([^A-Za-z0-9_]|$)/) {
                pending_flash = 1
                continue
            }

            if (inflash && depth == 1 &&
                piece ~ /^[[:space:]]*(size|reg)[[:space:]]*=/) {
                printf "  %s:%d: %s\n", FILENAME, i, raw[i]
                bad = 1
            }
        }
    }
    exit bad
}
